#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <apic/ioapic.hpp>
#include <drivers/storage/ahci.hpp>
#include <hpet/hpet.hpp>
#include <idt/idt.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <memory/paging.hpp>
#include <panic/panic.hpp>
#include <pci/pci.hpp>

namespace drivers
{

namespace storage
{

namespace ahci
{

static Controller controller_storage[MAX_CONTROLLERS];
static int controllers_found = 0;

Controller& controller = controller_storage[0];

// per-slot command tables are laid out 256 bytes apart; 64 (cfis) + 16 (acmd)
// + 48 (rsv) leaves room for 8 PRDT entries of 16 bytes each
static constexpr int MAX_PRDT = 8;
// each PRDT entry can describe up to 4MB (dbc is 22 bits, byte count minus 1)
static constexpr std::uint64_t PRDT_MAX_BYTES = 0x400000;
// the sector count in an LBA48 H2D FIS is 16 bits
static constexpr std::uint32_t MAX_SECTORS = 0xFFFF;
// port interrupt enable mask: D2H/PIO/DMA/SDB completions, hotplug, and every
// error class. all bits are within the PxIE write mask (0xfdc000ff).
static constexpr std::uint32_t PORT_IE =
    HBA_PxIS_DHRS | HBA_PxIS_PSS | HBA_PxIS_DSS | HBA_PxIS_SDBS | HBA_PxIS_DPS |
    HBA_PxIS_ERROR | HBA_PxIS_HOTPLUG;

static std::uint64_t identify_block_count(const std::uint8_t* buf)
{
	const std::uint16_t* w = reinterpret_cast<const std::uint16_t*>(buf);
	// word 83 bit 10: LBA48 addressing supported
	if (w[83] & (1 << 10))
	{
		std::uint64_t count = 0;
		for (int i = 100; i <= 103; ++i)
			count |= static_cast<std::uint64_t>(w[i])
				 << (16 * (i - 100));
		if (count != 0)
			return count;
	}
	// fall back to LBA28 (words 60-61)
	return static_cast<std::uint64_t>(w[60]) |
	       (static_cast<std::uint64_t>(w[61]) << 16);
}

static bool block_read(block::device* dev, std::uint64_t lba,
		       std::uint32_t count, void* buf)
{
	auto* d = static_cast<device_state*>(dev->priv);
	if (!d || !d->present || !d->port || !d->port->ahci)
		return false;
	return d->port->ahci->read(d, lba, count, buf);
}

static bool block_write(block::device* dev, std::uint64_t lba,
			std::uint32_t count, const void* buf)
{
	auto* d = static_cast<device_state*>(dev->priv);
	if (!d || !d->present || !d->port || !d->port->ahci)
		return false;
	return d->port->ahci->write(d, lba, count, buf);
}

static bool block_raw_read(block::device* dev, std::uint64_t lba,
			   std::uint32_t count, void* buf)
{
	return block_read(dev, lba, count, buf);
}

static bool block_raw_write(block::device* dev, std::uint64_t lba,
			    std::uint32_t count, const void* buf)
{
	return block_write(dev, lba, count, buf);
}

void ahci_irq_stub(interrupts::idt::frame* frame)
{
	(void)frame;
	for (int i = 0; i < controllers_found; ++i)
		controller_storage[i].irq();
}
// make prior writes to the ABAR visible to the controller before a command is
// issued, and the resulting DMA visible to the CPU before it is consumed
static void dma_barrier(void)
{
	asm volatile("mfence" : : : "memory");
}

// wait until `reg & mask` clears within timeout_us, using the HPET counter.
// returns false on timeout.
static bool wait_reg_clear(const volatile std::uint32_t& reg,
			   std::uint32_t mask, std::uint64_t timeout_us)
{
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	if (freq == 0)
		return false;

	std::uint64_t start = timers::hpet::hpet.read_counter();
	std::uint64_t ticks = freq / 1000000;
	if (ticks == 0)
		ticks = 1;

	while (reg & mask)
	{
		if (timers::hpet::hpet.read_counter() - start >=
		    timeout_us * ticks)
			return false;
		asm volatile("pause" : : : "memory");
	}
	return true;
}

// busy-wait for a number of microseconds using the HPET counter
static void wait_us(std::uint64_t us)
{
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	if (freq == 0)
		return;
	std::uint64_t ticks = freq / 1000000;
	if (ticks == 0)
		ticks = 1;
	std::uint64_t start = timers::hpet::hpet.read_counter();
	while (timers::hpet::hpet.read_counter() - start < us * ticks)
		asm volatile("pause" : : : "memory");
}

Controller::Controller()
{
	this->pci_bus = 0;
	this->pci_slot = 0;
	this->pci_func = 0;
	this->irq_line = 0;
	this->irq_vector = 0;
	this->use_msi = false;
	this->use_msix = false;
	this->msix_table.virt = nullptr;
	this->msix_table.phys = 0;
	this->controller_address = 0;
	this->controller_size = 0;
	this->s64a = false;
	this->sncq = false;
	this->salp = false;
	this->ssc = false;
	this->spm = false;
	this->fbss = false;
	this->num_slots = 0;
	this->cap2_boh = false;
	this->cap2_sds = false;
	this->cap2_sadm = false;

	for (int i = 0; i < MAX_PORTS; ++i)
	{
		port_state* p = &this->ports[i];
		p->present = false;
		p->port_num = i;
		p->regs = nullptr;
		p->ahci = nullptr;
		p->cmd_list.virt = nullptr;
		p->cmd_list.phys = 0;
		p->recv_fis.virt = nullptr;
		p->recv_fis.phys = 0;
		p->cmd_tbl.virt = nullptr;
		p->cmd_tbl.phys = 0;
		p->num_slots = 0;
		p->slots_issued = 0;
		p->sact_issued = 0;
		p->slots_done = 0;
		p->tfes = false;
		p->last_error = 0;
		p->last_err_reg = 0;
		p->has_pmp = false;
		p->fbs_enabled = false;
		for (int j = 0; j < MAX_PMP_DEVS; ++j)
		{
			device_state* d = &p->devices[j];
			d->present = false;
			d->type = AHCI_DEV_NULL;
			d->port = nullptr;
			d->pmp = j;
			d->block_count = 0;
			d->block_size = 512;
			d->ncq = false;
			d->queue_depth = 0;
			d->read_only = false;
			d->trim_supported = false;
			d->model[0] = 0;
			d->serial[0] = 0;
		}
	}
}

void Controller::init(void)
{
	init_all();
}

void init_all(void)
{
	if (controllers_found > 0)
		return;

	interrupts::idt::register_irq_handler(DEVICE_IRQ_VECTOR, ahci_irq_stub);

	int dev_count = pci::pci.device_count();
	for (int i = 0; i < dev_count && controllers_found < MAX_CONTROLLERS;
	     ++i)
	{
		const pci::device* device = pci::pci.get_device(i);
		if (!device)
			continue;
		// class 0x01 (mass storage), subclass 0x06 (SATA), prog-if 0x01
		// (AHCI)
		if (device->class_code == PCI_CLASS_MASS_STORAGE &&
		    device->subclass == 0x06 && device->prog_if == 0x01)
		{
			// make the controller visible to the IRQ stub before
			// init_one() issues any commands: the poll loops inside
			// probe_port() rely on the completion interrupt
			++controllers_found;
			controller_storage[controllers_found - 1].init_one(device);
		}
	}

	if (controllers_found == 0)
	{
#ifdef DEBUG
		LOG("no_ahci_controller_found");
#endif
		return;
	}
}

void Controller::init_one(const pci::device* device)
{
	this->pci_bus = device->bus;
	this->pci_slot = device->slot;
	this->pci_func = device->func;

	// enable bus master + memory space in the PCI command register (AHCI is
	// memory mapped, no IO space needed)
	std::uint32_t cmd =
	    pci::pci.read_config(this->pci_bus, this->pci_slot, this->pci_func,
				 PCI_COMMAND);
	cmd |= 0x6; // mem space, bus master
	pci::pci.write_config(this->pci_bus, this->pci_slot, this->pci_func,
			      PCI_COMMAND, cmd);

	pci::bar_info bar = pci::pci.read_bar_info(
	    this->pci_bus, this->pci_slot, this->pci_func, 5);
	if (bar.is_io || bar.address == 0 || bar.size < 0x100)
	{
		LOG("abar_unusable; bus=%02x; slot=%02x; func=%x",
		    static_cast<std::uint64_t>(this->pci_bus),
		    static_cast<std::uint64_t>(this->pci_slot),
		    static_cast<std::uint64_t>(this->pci_func));
		return;
	}

	this->controller_address = bar.address;
	this->controller_size = bar.size;

	memory::map_mmio_range(bar.address, bar.address + memory::hhdm_offset,
			       bar.size);

	std::uint32_t int_line = pci::pci.read_config(
	    this->pci_bus, this->pci_slot, this->pci_func, PCI_INTERRUPT_LINE);
	this->irq_line = int_line & 0xFF;

	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	// read capabilities once and honor them instead of assuming fixed
	// silicon behavior
	std::uint32_t cap = abar->cap;
	this->s64a = (cap & HBA_CAP_S64A) != 0;
	this->sncq = (cap & HBA_CAP_SNCQ) != 0;
	this->salp = (cap & HBA_CAP_SALP) != 0;
	this->ssc = (cap & HBA_CAP_SSC) != 0;
	this->spm = (cap & HBA_CAP_SPM) != 0;
	this->fbss = (cap & HBA_CAP_FBSS) != 0;
	// NCS is a 0-based slot count (QEMU reports 31 -> 32 slots)
	this->num_slots = ((cap & HBA_CAP_NCS_MASK) >> HBA_CAP_NCS_SHIFT) + 1;
	if (this->num_slots < 1 || this->num_slots > 32)
		this->num_slots = 32;

	std::uint32_t cap2 = abar->cap2;
	this->cap2_boh = (cap2 & HBA_CAP2_BOH) != 0;
	this->cap2_sds = (cap2 & HBA_CAP2_SDS) != 0;
	this->cap2_sadm = (cap2 & HBA_CAP2_SADM) != 0;

	this->setup_interrupts(abar);

	if (this->cap2_boh)
		this->bohc_handoff(abar);

	this->reset_controller(abar);

	abar->ghc |= HBA_GHC_AE; // AHCI enable
	abar->ghc |= HBA_GHC_IE; // global interrupt enable

	abar->is = static_cast<std::uint32_t>(-1); // clear pending global IS

	this->probe_port(abar);

#ifdef DEBUG
	LOG("ahci_controller_ready; bus=%02x; slot=%02x; func=%x; abar=%p; "
	    "size=0x%x; slots=%d; irq=%u; s64a=%d; sncq=%d; spm=%d; fbss=%d",
	    static_cast<std::uint64_t>(this->pci_bus),
	    static_cast<std::uint64_t>(this->pci_slot),
	    static_cast<std::uint64_t>(this->pci_func),
	    static_cast<std::uintptr_t>(this->controller_address),
	    static_cast<std::uint64_t>(this->controller_size),
	    static_cast<std::uint64_t>(this->num_slots),
	    static_cast<std::uint64_t>(this->irq_line),
	    this->s64a ? 1 : 0, this->sncq ? 1 : 0, this->spm ? 1 : 0,
	    this->fbss ? 1 : 0);
#endif
}

void Controller::setup_interrupts(volatile hba_mem* abar)
{
	(void)abar;
	this->irq_vector = DEVICE_IRQ_VECTOR;

	// MSI-X first (most flexible), then MSI, then shared INTx on the IOAPIC
	this->use_msix = this->msix_enable();
	this->use_msi = false;
	if (!this->use_msix)
		this->use_msi = this->msi_enable();

	if (this->use_msix || this->use_msi)
	{
		interrupts::idt::register_irq_handler(this->irq_vector,
						      ahci_irq_stub);
	}
	else
	{
		this->intx_setup();
	}
}

// program the MSI capability: single edge-triggered message routed to the
// boot CPU. returns true only if the capability was found and the enable bit
// stuck.
bool Controller::msi_enable(void)
{
	std::uint8_t cap = pci::pci.find_capability(
	    this->pci_bus, this->pci_slot, this->pci_func, PCI_CAP_MSI);
	if (cap == 0)
		return false;

	std::uint16_t msg_ctrl = pci::pci.read_config16(
	    this->pci_bus, this->pci_slot, this->pci_func, cap + 2);
	bool is_64 = (msg_ctrl & PCI_MSI_CTRL_64BIT) != 0;

	// one message, disabled until fully programmed
	msg_ctrl &= ~PCI_MSI_CTRL_MME_MASK;
	msg_ctrl &= ~PCI_MSI_CTRL_ENABLE;

	std::uint8_t dest = interrupts::apic::apic.get_id();
	std::uint32_t msg_addr;
	if (interrupts::apic::apic.is_x2apic_enabled())
	{
		// x2APIC: destination ID lives in bits 11..4
		msg_addr = 0xFEE00000 | (static_cast<std::uint32_t>(dest) << 4);
	}
	else
	{
		// xAPIC: destination ID lives in bits 19..12
		msg_addr =
		    0xFEE00000 | (static_cast<std::uint32_t>(dest) << 12);
	}

	std::uint32_t msg_data = this->irq_vector; // fixed delivery, edge

	pci::pci.write_config(this->pci_bus, this->pci_slot, this->pci_func,
			      cap + 4, msg_addr);
	if (is_64)
		pci::pci.write_config(this->pci_bus, this->pci_slot,
				      this->pci_func, cap + 8, 0);

	std::uint8_t data_off = is_64 ? cap + 12 : cap + 8;
	pci::pci.write_config(this->pci_bus, this->pci_slot, this->pci_func,
			      data_off, msg_data);

	msg_ctrl |= PCI_MSI_CTRL_ENABLE;
	pci::pci.write_config16(this->pci_bus, this->pci_slot, this->pci_func,
				cap + 2, msg_ctrl);

	// verify the enable bit took before relying on MSI delivery
	std::uint16_t check = pci::pci.read_config16(
	    this->pci_bus, this->pci_slot, this->pci_func, cap + 2);
	if (!(check & PCI_MSI_CTRL_ENABLE))
		return false;

#ifdef DEBUG
	LOG("msi_enabled; bus=%02x; addr=0x%08x; data=0x%08x; dest=%u",
	    static_cast<std::uint64_t>(this->pci_bus),
	    static_cast<std::uint64_t>(msg_addr),
	    static_cast<std::uint64_t>(msg_data),
	    static_cast<std::uint64_t>(dest));
#endif
	return true;
}

// MSI-X: a per-entry table in one of the controller's BARs. entry 0 is
// programmed with the device-IRQ vector and unmasked. falls back (enable bit
// cleared) if the capability, BAR, or table is unusable.
bool Controller::msix_enable(void)
{
	std::uint8_t cap = pci::pci.find_capability(
	    this->pci_bus, this->pci_slot, this->pci_func, PCI_CAP_MSIX);
	if (cap == 0)
		return false;

	std::uint16_t msg_ctrl = pci::pci.read_config16(
	    this->pci_bus, this->pci_slot, this->pci_func, cap + 2);

	std::uint32_t table = pci::pci.read_config(
	    this->pci_bus, this->pci_slot, this->pci_func, cap + 4);
	int bar_index = table & 0x7;
	std::uint32_t offset = table & ~0x7;

	pci::bar_info bar = pci::pci.read_bar_info(
	    this->pci_bus, this->pci_slot, this->pci_func, bar_index);
	if (bar.is_io || bar.address == 0)
		return false;

	memory::map_mmio_range(bar.address, bar.address + memory::hhdm_offset,
			       bar.size);

	auto* table_virt = static_cast<volatile std::uint8_t*>(
	    memory::phys_to_virt(bar.address + offset));

	std::uint8_t dest = interrupts::apic::apic.get_id();
	std::uint32_t msg_addr;
	if (interrupts::apic::apic.is_x2apic_enabled())
		msg_addr = 0xFEE00000 | (static_cast<std::uint32_t>(dest) << 4);
	else
		msg_addr = 0xFEE00000 | (static_cast<std::uint32_t>(dest) << 12);
	std::uint32_t msg_data = this->irq_vector;

	// entry layout: 8-byte address, 4-byte data, 4-byte vector control
	*reinterpret_cast<volatile std::uint32_t*>(table_virt + 0) = msg_addr;
	*reinterpret_cast<volatile std::uint32_t*>(table_virt + 4) =
	    static_cast<std::uint32_t>(static_cast<std::uint64_t>(msg_addr) >>
				      32);
	*reinterpret_cast<volatile std::uint32_t*>(table_virt + 8) = msg_data;
	*reinterpret_cast<volatile std::uint32_t*>(table_virt + 12) =
	    0; // unmasked

	// enable MSI-X, clear function mask
	msg_ctrl |= (1 << 15);
	msg_ctrl &= ~(1 << 14);
	pci::pci.write_config16(this->pci_bus, this->pci_slot, this->pci_func,
				cap + 2, msg_ctrl);

	std::uint16_t check = pci::pci.read_config16(
	    this->pci_bus, this->pci_slot, this->pci_func, cap + 2);
	if (!(check & (1 << 15)))
	{
		msg_ctrl &= ~(1 << 15);
		pci::pci.write_config16(this->pci_bus, this->pci_slot,
					this->pci_func, cap + 2, msg_ctrl);
		return false;
	}

	this->msix_table.virt = const_cast<std::uint8_t*>(table_virt);
	this->msix_table.phys = bar.address + offset;

#ifdef DEBUG
	LOG("msix_enabled; bus=%02x; bar=%d; offset=0x%x; data=0x%08x",
	    static_cast<std::uint64_t>(this->pci_bus),
	    static_cast<std::uint64_t>(bar_index),
	    static_cast<std::uint64_t>(offset),
	    static_cast<std::uint64_t>(msg_data));
#endif
	return true;
}

// fallback path: route the legacy INTx line through the IOAPIC. the PCI
// interrupt line maps to a GSI (after ACPI ISO overrides), which lands on a
// fixed IOAPIC redirection pin. PCI interrupts are level-triggered and
// active-low.
void Controller::intx_setup(void)
{
	std::uint32_t gsi =
	    acpi::acpi.resolve_irq(static_cast<std::uint8_t>(this->irq_line));
	if (gsi < acpi::acpi.ioapic_gsi_base ||
	    gsi >= acpi::acpi.ioapic_gsi_base +
		       interrupts::ioapic::ioapic.max_pins())
	{
#ifdef DEBUG
		LOG("intx_unroutable; irq_line=%u; gsi=%u; base=%u",
		    static_cast<std::uint64_t>(this->irq_line),
		    static_cast<std::uint64_t>(gsi),
		    static_cast<std::uint64_t>(acpi::acpi.ioapic_gsi_base));
#endif
		return;
	}

	std::uint8_t pin =
	    static_cast<std::uint8_t>(gsi - acpi::acpi.ioapic_gsi_base);

	interrupts::idt::register_irq_handler(this->irq_vector, ahci_irq_stub);

	interrupts::ioapic::ioapic.redirect_irq(
	    pin, this->irq_vector, interrupts::apic::apic.get_id(),
	    true /* level */, true /* active low */);
	interrupts::ioapic::ioapic.unmask_irq(pin);

#ifdef DEBUG
	LOG("intx_routed; irq_line=%u; gsi=%u; pin=%u; vector=0x%x",
	    static_cast<std::uint64_t>(this->irq_line),
	    static_cast<std::uint64_t>(gsi), static_cast<std::uint64_t>(pin),
	    static_cast<std::uint64_t>(this->irq_vector));
#endif
}

void Controller::bohc_handoff(volatile hba_mem* abar)
{
	if (!(abar->bohc & HBA_BOHC_BOS))
	{
#ifdef DEBUG
		LOG("bohc_handoff_success");
#endif
		return;
	}

	abar->bohc |= HBA_BOHC_OOS;

	if (!wait_reg_clear(abar->bohc, HBA_BOHC_BOS, CONTROLLER_TIMEOUT_US))
	{
#ifdef DEBUG
		LOG("bohc_handoff_timeout; bios_did_not_release");
#endif
	}

	if (abar->bohc & HBA_BOHC_BB)
		abar->bohc |= HBA_BOHC_BB;
#ifdef DEBUG
	LOG("bohc_handoff_success");
#endif
}

void Controller::reset_controller(volatile hba_mem* abar)
{
	abar->ghc |= HBA_GHC_HR;

	if (!wait_reg_clear(abar->ghc, HBA_GHC_HR, CONTROLLER_TIMEOUT_US))
	{
#ifdef DEBUG
		LOG("reset_timeout; ghc=0x%x", abar->ghc);
#endif
	}
}

void Controller::probe_port(volatile hba_mem* abar)
{
	std::uint32_t pi = abar->pi;
	for (int i = 0; i < MAX_PORTS; ++i)
	{
		if (!(pi & 1))
		{
			pi >>= 1;
			continue;
		}

		port_state* p = &this->ports[i];
		p->present = true;
		p->port_num = i;
		p->regs = &abar->ports[i];
		p->ahci = this;
		p->slots_issued = 0;
		p->sact_issued = 0;
		p->slots_done = 0;
		p->tfes = false;
		p->last_error = 0;
		p->last_err_reg = 0;
		p->has_pmp = false;
		p->fbs_enabled = false;
		p->num_slots = this->num_slots;

		this->port_rebase(p);

		int type = this->check_type(p->regs);
		if (type == AHCI_DEV_SATAPI)
			p->regs->cmd |= HBA_PxCMD_ATAPI;

		this->init_power_management(p);

		if (type == AHCI_DEV_SATA || type == AHCI_DEV_SATAPI)
			this->register_devices(p);
#ifdef DEBUG
		else
			LOG("no_device_at_port=%d; type=%d", i, type);
#endif
		pi >>= 1;
	}
}

int Controller::check_type(volatile hba_port* port)
{
	std::uint32_t ssts = port->ssts;
	std::uint8_t ipm = (ssts >> 8) & 0x0F;
	std::uint8_t det = ssts & 0x0F;
	if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
		return AHCI_DEV_NULL;

	switch (port->sig)
	{
	case SATA_SIG_ATAPI:
		return AHCI_DEV_SATAPI;
	case SATA_SIG_SEMB:
		return AHCI_DEV_SEMB;
	case SATA_SIG_PM:
		return AHCI_DEV_PM;
	default:
		return AHCI_DEV_SATA;
	}
}

void Controller::port_rebase(port_state* p)
{
	volatile hba_port* port = p->regs;
	this->stop_cmd(port);

	// Allocate command list (1K = 32 entries * 32 bytes)
	p->cmd_list = dma::alloc(1024);
	if (!p->cmd_list.virt)
	{
#ifdef DEBUG
		LOG("cmd_list_alloc_failed; port=%d", p->port_num);
#endif
		return;
	}
	port->clb = static_cast<std::uint32_t>(p->cmd_list.phys);
	port->clbu = static_cast<std::uint32_t>(p->cmd_list.phys >> 32);
	memory::memset(p->cmd_list.virt, 0, 1024);

	// Allocate received FIS (256 bytes)
	p->recv_fis = dma::alloc(256);
	if (!p->recv_fis.virt)
	{
#ifdef DEBUG
		LOG("recv_fis_alloc_failed; port=%d", p->port_num);
#endif
		return;
	}
	port->fb = static_cast<std::uint32_t>(p->recv_fis.phys);
	port->fbu = static_cast<std::uint32_t>(p->recv_fis.phys >> 32);
	memory::memset(p->recv_fis.virt, 0, 256);

	// Allocate command tables (8K = 32 entries * 256 bytes each)
	p->cmd_tbl = dma::alloc(8192);
	if (!p->cmd_tbl.virt)
	{
#ifdef DEBUG
		LOG("cmd_tbl_alloc_failed; port=%d", p->port_num);
#endif
		return;
	}

	auto* cmd_header = static_cast<hba_cmd_header*>(p->cmd_list.virt);
	for (int i = 0; i < p->num_slots; ++i)
	{
		cmd_header[i].prdtl = MAX_PRDT;
		std::uint64_t entry_phys = p->cmd_tbl.phys + i * 256;
		cmd_header[i].ctba = static_cast<std::uint32_t>(entry_phys);
		cmd_header[i].ctbau =
		    static_cast<std::uint32_t>(entry_phys >> 32);
		memory::memset(static_cast<std::uint8_t*>(p->cmd_tbl.virt) +
				   i * 256,
			       0, 256);
	}

	// the controller reads the command list/tables and writes the received
	// FIS area; make the CPU's writes visible and drop stale lines so the
	// controller's DMA results are read fresh
	dma::cache_flush(p->cmd_list.virt, 1024);
	dma::cache_flush(p->cmd_tbl.virt, 8192);
	dma::cache_invalidate(p->recv_fis.virt, 256);

	dma_barrier();

	port->cmd |= HBA_PxCMD_SUD; // spin up device
	port->cmd |= HBA_PxCMD_POD; // power on device

	this->start_cmd(port);

	port->is = static_cast<std::uint32_t>(-1); // clear pending interrupts
	port->ie = PORT_IE;

#ifdef DEBUG
	LOG("port=%i; clb=%p; fb=%p; ct=%p; slots=%d", p->port_num,
	    static_cast<std::uintptr_t>(p->cmd_list.phys),
	    static_cast<std::uintptr_t>(p->recv_fis.phys),
	    static_cast<std::uintptr_t>(p->cmd_tbl.phys), p->num_slots);
#endif
}

void Controller::start_cmd(volatile hba_port* port)
{
	// wait until CR (bit 15) is cleared
	if (!wait_reg_clear(port->cmd, HBA_PxCMD_CR, PORT_TIMEOUT_US))
	{
#ifdef DEBUG
		LOG("start_cmd_cr_timeout; cmd=0x%x", port->cmd);
#endif
	}
	// set FRE (bit 4) and ST (bit 0)
	port->cmd |= HBA_PxCMD_FRE;
	dma_barrier();
	port->cmd |= HBA_PxCMD_ST;
}

void Controller::stop_cmd(volatile hba_port* port)
{
	// clear ST (bit 0)
	port->cmd &= ~HBA_PxCMD_ST;
	// clear FRE (bit 4)
	port->cmd &= ~HBA_PxCMD_FRE;

	// wait until FR (bit 14) and CR (bit 15) are cleared
	if (!wait_reg_clear(port->cmd, HBA_PxCMD_FR | HBA_PxCMD_CR,
			    PORT_TIMEOUT_US))
	{
#ifdef DEBUG
		LOG("stop_cmd_timeout; cmd=0x%x", port->cmd);
#endif
	}
}

int Controller::alloc_slot(port_state* p)
{
	for (int i = 0; i < p->num_slots; ++i)
	{
		std::uint32_t bit = static_cast<std::uint32_t>(1) << i;
		if (!(p->slots_issued & bit) && !(p->sact_issued & bit))
		{
			p->slots_issued |= bit;
			return i;
		}
	}
	return -1;
}

bool Controller::wait_slot(port_state* p, std::uint32_t mask,
			   std::uint64_t timeout_us)
{
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	if (freq == 0)
		return false;

	std::uint64_t ticks = freq / 1000000;
	if (ticks == 0)
		ticks = 1;

	std::uint64_t start = timers::hpet::hpet.read_counter();
	// the command completes either normally (D2H/SDB sets slots_done) or
	// with an error (TFES raised); in the error case the controller still
	// finished the command, so wake the issuer to consume the failure
	while (!(p->slots_done & mask) && !p->tfes)
	{
		if (timers::hpet::hpet.read_counter() - start >=
		    timeout_us * ticks)
			return false;
		asm volatile("pause" : : : "memory");
	}
	return true;
}

// light re-arm of the command engine: stop, clear error/interrupt state, and
// restart. keeps slot bookkeeping intact so an in-flight slot can be re-issued
// without losing its allocation.
void Controller::port_rearm(port_state* p)
{
	volatile hba_port* port = p->regs;
	this->stop_cmd(port);
	port->serr = static_cast<std::uint32_t>(-1); // clear SERR
	port->is = static_cast<std::uint32_t>(-1);   // clear port interrupts
	this->start_cmd(port);
	p->tfes = false;
}

// full software recovery: re-arm the engine and forget all slot state
void Controller::port_recover(port_state* p)
{
	this->port_rearm(p);
	p->slots_issued = 0;
	p->sact_issued = 0;
	p->slots_done = 0;
	p->tfes = false;
}

// SATA COMRESET per the AHCI spec (s10.4.1): pulse PxSCTL.DET=1 for >= 1ms
// so the HBA forces the link to reset, then release DET and wait for the
// device to re-establish communication (PxSSTS.DET=3). re-arms the command
// engine and clears all slot state on success.
bool Controller::port_reset(port_state* p)
{
	volatile hba_port* port = p->regs;

	this->stop_cmd(port);

	// force the link into the reset state
	port->sctl = (port->sctl & ~HBA_PxSCTL_DET_MASK) | HBA_PxSCTL_DET_RESET;
	wait_us(1000); // COMRESET must be driven for at least 1ms
	// release the reset: DET=0 (no device-detection action, normal
	// operation)
	port->sctl = (port->sctl & ~HBA_PxSCTL_DET_MASK) | 0x0;

	// wait for the device to come back up on the link
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	std::uint64_t start = timers::hpet::hpet.read_counter();
	std::uint64_t ticks = (freq / 1000000) * PORT_TIMEOUT_US;
	if (ticks == 0)
		ticks = 1;
	while ((port->ssts & 0x0F) != HBA_PORT_DET_PRESENT)
	{
		if (timers::hpet::hpet.read_counter() - start >= ticks)
		{
#ifdef DEBUG
			LOG("port_reset_no_device; port=%d; ssts=0x%x",
			    p->port_num,
			    static_cast<std::uint64_t>(port->ssts));
#endif
			return false;
		}
		asm volatile("pause" : : : "memory");
	}

	port->serr = static_cast<std::uint32_t>(-1);
	port->is = static_cast<std::uint32_t>(-1);
	port->sact = 0;

	this->start_cmd(port);

	p->slots_issued = 0;
	p->sact_issued = 0;
	p->slots_done = 0;
	p->tfes = false;
	return true;
}

// link power management is deliberately conservative: ALPE/ASP and PxDEVSLP
// can wedge links on hardware that disagrees about CAP2.SDS, so nothing is
// enabled here. the capability bits read at init are left for policy use.
void Controller::init_power_management(port_state* p)
{
	(void)p;
	(void)this->cap2_sds;
	(void)this->cap2_sadm;
}

bool Controller::issue_command(port_state* p, int pmp, const issue_desc& desc)
{
	volatile hba_port* port = p->regs;
	if (!port)
		return false;

	p->lock.lock();

	int slot = this->alloc_slot(p);
	if (slot < 0)
	{
		p->lock.unlock();
		return false;
	}
	std::uint32_t mask = static_cast<std::uint32_t>(1) << slot;

	if (desc.ncq)
	{
		p->slots_issued &= ~mask;
		p->sact_issued |= mask;
	}

	auto* header =
	    static_cast<hba_cmd_header*>(p->cmd_list.virt) + slot;
	auto* tbl = reinterpret_cast<hba_cmd_tbl*>(
	    static_cast<std::uint8_t*>(p->cmd_tbl.virt) + slot * 256);

	memory::memset(tbl, 0, sizeof(hba_cmd_tbl));

	// PRDT: split the (contiguous) DMA buffer into up to 4MB entries
	std::uint64_t dbc_phys = desc.buf.phys;
	std::size_t left = desc.data_bytes;
	int nprdt = 0;
	while (left > 0 && nprdt < MAX_PRDT)
	{
		std::size_t take =
		    left > PRDT_MAX_BYTES ? PRDT_MAX_BYTES : left;
		tbl->prdt_entry[nprdt].dba =
		    static_cast<std::uint32_t>(dbc_phys);
		tbl->prdt_entry[nprdt].dbau =
		    static_cast<std::uint32_t>(dbc_phys >> 32);
		tbl->prdt_entry[nprdt].dbc =
		    static_cast<std::uint32_t>(take - 1);
		tbl->prdt_entry[nprdt].i = 0;
		dbc_phys += take;
		left -= take;
		++nprdt;
	}
	if (left > 0)
	{
		// buffer too large for this command table's PRD capacity
		p->lock.unlock();
		return false;
	}
	if (nprdt > 0)
		tbl->prdt_entry[nprdt - 1].i = 1; // interrupt on last entry

	// H2D FIS: LBA48 command
	auto* fis = reinterpret_cast<reg_h2d*>(tbl->cfis);
	fis->fis_type = static_cast<std::uint8_t>(fis_type::FIS_TYPE_REG_H2D);
	fis->c = 1;
	fis->pmport = static_cast<std::uint8_t>(pmp) & 0x0F;
	fis->command = desc.command;
	fis->featurel = desc.feature & 0xFF;
	fis->lba0 = static_cast<std::uint8_t>(desc.lba);
	fis->lba1 = static_cast<std::uint8_t>(desc.lba >> 8);
	fis->lba2 = static_cast<std::uint8_t>(desc.lba >> 16);
	fis->device = 1 << 6; // LBA addressing
	fis->lba3 = static_cast<std::uint8_t>(desc.lba >> 24);
	fis->lba4 = static_cast<std::uint8_t>(desc.lba >> 32);
	fis->lba5 = static_cast<std::uint8_t>(desc.lba >> 40);
	if (desc.atapi)
	{
		// PACKET: bytes 4-7 carry no LBA; the byte count limit (BCL)
		// goes in lcyl/hcyl (bytes 5-6). the device transfers data in
		// phases of at most BCL bytes, so the maximum value is used to
		// let the PRDT size drive the transfer.
		fis->lba0 = 0;
		fis->lba1 = 0xFF; // BCL low
		fis->lba2 = 0xFE; // BCL high (0xfffe)
		fis->lba3 = 0;
		fis->lba4 = 0;
		fis->lba5 = 0;
	}
	if (desc.ncq)
	{
		// NCQ H2D: sector count lives in featurel/featureh (bytes 3 and
		// 11) and the tag in countl (byte 12, bits 7:3)
		fis->featurel = desc.sectors & 0xFF;
		fis->featureh = (desc.sectors >> 8) & 0xFF;
		fis->countl = static_cast<std::uint8_t>((slot << 3) & 0xF8);
		fis->counth = 0;
	}
	else
	{
		fis->countl = static_cast<std::uint8_t>(desc.sectors);
		fis->counth = static_cast<std::uint8_t>(desc.sectors >> 8);
	}

	header->cfl = sizeof(reg_h2d) / 4;
	header->w = desc.to_device ? 1 : 0; // host-to-device for writes
	header->a = desc.atapi ? 1 : 0;
	header->prdtl = static_cast<std::uint16_t>(nprdt);
	header->pmp = static_cast<std::uint16_t>(pmp) & 0x0F;

	if (desc.atapi && desc.packet != nullptr)
	{
		std::size_t plen = desc.packet_len;
		if (plen > sizeof(tbl->acmd))
			plen = sizeof(tbl->acmd);
		memory::memset(tbl->acmd, 0, sizeof(tbl->acmd));
		memory::memcpy(tbl->acmd, desc.packet, plen);
	}

	// make the freshly built command list/tables visible to the controller
	dma::cache_flush(header, sizeof(hba_cmd_header));
	dma::cache_flush(tbl, sizeof(hba_cmd_tbl));

	for (int attempt = 0; attempt < MAX_RETRIES; ++attempt)
	{
		// clear pending port status, then issue the command on the slot
		port->is = static_cast<std::uint32_t>(-1);
		p->slots_done &= ~mask;
		p->tfes = false;

		dma_barrier();

		if (desc.ncq)
		{
			// NCQ: tag mask goes into PxSACT first, then PxCI. the
			// HBA clears PxCI once the command is fetched and the
			// device signals completion with a Set Device Bits FIS.
			port->sact |= mask;
			dma_barrier();
			port->ci |= mask;
		}
		else
		{
			port->ci = mask;
		}

		p->lock.unlock();

		bool ok = this->wait_slot(p, mask, CMD_TIMEOUT_US);
		if (!ok)
		{
#ifdef DEBUG
			auto* abar_tmp = static_cast<volatile hba_mem*>(
			    memory::phys_to_virt(
				this->controller_address));
			dma::cache_invalidate(header, sizeof(hba_cmd_header));
			LOG("cmd_timeout; port=%d; slot=%d; cmd=0x%x; "
			    "ghc_is=0x%x; pis=0x%x; ci=0x%x; tfd=0x%x; "
			    "ssts=0x%x; prdbc=%u; attempt=%d",
			    p->port_num, slot,
			    static_cast<std::uint64_t>(desc.command),
			    static_cast<std::uint64_t>(abar_tmp->is),
			    static_cast<std::uint64_t>(port->is),
			    static_cast<std::uint64_t>(port->ci),
			    static_cast<std::uint64_t>(port->tfd),
			    static_cast<std::uint64_t>(port->ssts),
			    static_cast<std::uint64_t>(header->prdbc),
			    attempt);
#endif
		}
		else if (p->tfes || (port->tfd & 0x1)) // PxTFD bit 0 = ERR
		{
#ifdef DEBUG
			LOG("cmd_error; port=%d; slot=%d; cmd=0x%x; tfd=0x%x; "
			    "serr=0x%x; err_reg=0x%x; attempt=%d",
			    p->port_num, slot,
			    static_cast<std::uint64_t>(desc.command),
			    static_cast<std::uint64_t>(port->tfd),
			    static_cast<std::uint64_t>(port->serr),
			    static_cast<std::uint64_t>(p->last_err_reg),
			    attempt);
#endif
			ok = false;
		}
		else
		{
			// the controller wrote the PRD byte count into the
			// command header; read it fresh and require it to match
			// the expected transfer size
			dma::cache_invalidate(header, sizeof(hba_cmd_header));
			if (header->prdbc != desc.data_bytes)
			{
#ifdef DEBUG
				LOG("prd_byte_count_mismatch; port=%d; "
				    "slot=%d; "
				    "expected=%u; prdbc=%u; attempt=%d",
				    p->port_num, slot,
				    static_cast<std::uint64_t>(desc.data_bytes),
				    static_cast<std::uint64_t>(header->prdbc),
				    attempt);
#endif
				ok = false;
			}
		}

		p->lock.lock();
		if (ok)
		{
			p->slots_issued &= ~mask;
			p->sact_issued &= ~mask;
			p->lock.unlock();
			return true;
		}

		// re-arm the command engine between attempts (the slot stays
		// allocated); the final failure does the full SATA reset
		if (attempt + 1 < MAX_RETRIES)
			this->port_rearm(p);
	}

	// every attempt failed: full SATA reset (COMRESET) of the port
	this->port_reset(p);
	p->lock.unlock();
	return false;
}

bool Controller::issue_device_command(device_state* d, std::uint64_t lba,
				      std::uint32_t sectors, dma::addr buf,
				      bool to_device)
{
	if (!d || !d->present)
		return false;
	port_state* p = d->port;
	if (!p || !p->present)
		return false;

	std::size_t block_size = d->block_size ? d->block_size : 512;

	issue_desc desc{};
	desc.lba = lba;
	desc.sectors = sectors;
	desc.data_bytes =
	    static_cast<std::uint32_t>(static_cast<std::uint64_t>(sectors) *
				       block_size);
	desc.buf = buf;
	desc.to_device = to_device;

	if (d->type == AHCI_DEV_SATAPI)
	{
		desc.atapi = true;
		desc.command = ATA_CMD_PACKET;
		std::uint8_t packet[12] = {};
		packet[0] = to_device ? ATAPI_WRITE10 : ATAPI_READ10;
		packet[2] = static_cast<std::uint8_t>(lba >> 24);
		packet[3] = static_cast<std::uint8_t>(lba >> 16);
		packet[4] = static_cast<std::uint8_t>(lba >> 8);
		packet[5] = static_cast<std::uint8_t>(lba);
		packet[7] = static_cast<std::uint8_t>(sectors >> 8);
		packet[8] = static_cast<std::uint8_t>(sectors);
		desc.packet = packet;
		desc.packet_len = 12;
		return this->issue_command(p, d->pmp, desc);
	}

	if (d->ncq)
	{
		desc.ncq = true;
		desc.command =
		    to_device ? ATA_CMD_WRITE_FPDMA : ATA_CMD_READ_FPDMA;
	}
	else
	{
		desc.command =
		    to_device ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
	}
	return this->issue_command(p, d->pmp, desc);
}

bool Controller::transfer(device_state* d, std::uint64_t lba,
			  std::uint32_t sectors, void* buf, bool to_device)
{
	if (!d || !d->present)
		return false;
	port_state* p = d->port;
	if (!p || !p->present)
		return false;
	if (sectors == 0)
		return true;
	if (d->block_count != 0 && lba + sectors > d->block_count)
		return false;

	std::size_t block_size = d->block_size ? d->block_size : 512;
	std::size_t total_bytes =
	    static_cast<std::size_t>(sectors) * block_size;

	// the DMA buffer must be physically contiguous; the caller's buffer is
	// kernel virtual memory that need not be, so copy through a dma buffer
	dma::addr dma_buf = dma::alloc(total_bytes);
	if (!dma_buf.virt)
		return false;

	auto* data = static_cast<std::uint8_t*>(buf);
	if (to_device)
	{
		memory::memcpy(dma_buf.virt, data, total_bytes);
		// make the staged data visible to the DMA engine before issuing
		dma::cache_flush(dma_buf.virt, total_bytes);
	}

	// per-command sector cap: LBA48 count is 16 bits, NCQ count is 8 bits,
	// and the 8-entry PRDT table moves at most 32MB
	std::uint64_t max_bytes =
	    static_cast<std::uint64_t>(MAX_PRDT) * PRDT_MAX_BYTES;
	std::uint64_t max_chunk = max_bytes / block_size;
	if (max_chunk > MAX_SECTORS)
		max_chunk = MAX_SECTORS;
	if (d->ncq && max_chunk > 255)
		max_chunk = 255;

	std::uint64_t remaining = sectors;
	std::uint64_t cur_lba = lba;
	std::uint64_t offset = 0;
	bool ok = true;

	while (remaining > 0)
	{
		std::uint32_t chunk =
		    remaining > max_chunk
			? static_cast<std::uint32_t>(max_chunk)
			: static_cast<std::uint32_t>(remaining);

		dma::addr chunk_buf;
		chunk_buf.virt =
		    static_cast<std::uint8_t*>(dma_buf.virt) + offset;
		chunk_buf.phys = dma_buf.phys + offset;

		ok = this->issue_device_command(d, cur_lba, chunk, chunk_buf,
						to_device);
		if (!ok)
			break;

		remaining -= chunk;
		cur_lba += chunk;
		offset += static_cast<std::uint64_t>(chunk) * block_size;
	}

	if (ok && !to_device)
	{
		// the device wrote this buffer; drop stale cache lines so the
		// CPU re-reads the DMA'd data
		dma::cache_invalidate(dma_buf.virt, total_bytes);
		memory::memcpy(data, dma_buf.virt, total_bytes);
	}

	dma::free(dma_buf, total_bytes);
	return ok;
}

bool Controller::read(device_state* d, std::uint64_t lba, std::uint32_t count,
		      void* buf)
{
	return this->transfer(d, lba, count, buf, false);
}

bool Controller::write(device_state* d, std::uint64_t lba, std::uint32_t count,
		       const void* buf)
{
	return this->transfer(d, lba, count, const_cast<void*>(buf), true);
}

bool Controller::trim(device_state* d, const std::uint64_t* lbas,
		      const std::uint64_t* counts, int n)
{
	if (!d || !d->present || n <= 0 || lbas == nullptr || counts == nullptr)
		return false;
	if (!d->trim_supported)
		return false;
	port_state* p = d->port;
	if (!p || !p->present)
		return false;

	// one 512-byte block of LBA48 range entries: 48-bit LBA + 16-bit count
	dma::addr buf = dma::alloc(512);
	if (!buf.virt)
		return false;

	bool ok = true;
	for (int i = 0; i < n; ++i)
	{
		auto* range = static_cast<std::uint8_t*>(buf.virt);
		std::uint64_t entry =
		    (lbas[i] & 0x0000FFFFFFFFFFFFull) |
		    ((counts[i] & 0xFFFF) << 48);
		for (int b = 0; b < 8; ++b)
			range[b] = static_cast<std::uint8_t>(entry >> (8 * b));
		memory::memset(range + 8, 0, 504);
		dma::cache_flush(buf.virt, 512);

		issue_desc desc{};
		desc.lba = 0;
		desc.sectors = 1;
		desc.data_bytes = 512;
		desc.buf = buf;
		desc.command = ATA_CMD_DSM;
		desc.feature = 1; // TRIM
		desc.to_device = true;
		ok = this->issue_command(p, d->pmp, desc);
		if (!ok)
			break;
	}

	dma::free(buf, 512);
	return ok;
}

bool Controller::smart_read_data(device_state* d, std::uint8_t* out)
{
	if (!d || !d->present)
		return false;
	port_state* p = d->port;
	if (!p || !p->present)
		return false;

	dma::addr buf = dma::alloc(512);
	if (!buf.virt)
		return false;

	issue_desc desc{};
	desc.lba = 0;
	desc.sectors = 1;
	desc.data_bytes = 512;
	desc.buf = buf;
	desc.command = ATA_CMD_SMART;
	desc.feature = ATA_SMART_READ_DATA;

	bool ok = this->issue_command(p, d->pmp, desc);
	if (ok)
	{
		dma::cache_invalidate(buf.virt, 512);
		memory::memcpy(out, buf.virt, 512);
	}

	dma::free(buf, 512);
	return ok;
}

bool Controller::identify(port_state* p, int pmp, device_state* d, int type)
{
	dma::addr buf = dma::alloc(512);
	if (!buf.virt)
		return false;

	issue_desc desc{};
	desc.lba = 0;
	desc.sectors = 1;
	desc.data_bytes = 512;
	desc.buf = buf;
	desc.command = (type == AHCI_DEV_SATAPI)
			   ? ATA_CMD_IDENTIFY_PACKET
			   : ATA_CMD_IDENTIFY_DEVICE;

	bool ok = this->issue_command(p, pmp, desc);
	if (ok)
	{
		dma::cache_invalidate(buf.virt, 512);
		this->identify_parse(d,
				     static_cast<const std::uint8_t*>(buf.virt));
	}

	dma::free(buf, 512);
	return ok;
}

void Controller::identify_parse(device_state* d, const std::uint8_t* buf)
{
	const std::uint16_t* w = reinterpret_cast<const std::uint16_t*>(buf);

	memory::memset(d->model, 0, sizeof(d->model));
	memory::memset(d->serial, 0, sizeof(d->serial));
	for (int i = 0; i < 20; ++i)
	{
		if (i * 2 + 1 < static_cast<int>(sizeof(d->model)))
			d->model[i * 2] =
			    static_cast<std::uint8_t>(w[27 + i] >> 8);
		if (i * 2 + 1 < static_cast<int>(sizeof(d->model)))
			d->model[i * 2 + 1] =
			    static_cast<std::uint8_t>(w[27 + i] & 0xFF);
	}
	for (int i = 0; i < 10; ++i)
	{
		if (i * 2 + 1 < static_cast<int>(sizeof(d->serial)))
			d->serial[i * 2] =
			    static_cast<std::uint8_t>(w[10 + i] >> 8);
		if (i * 2 + 1 < static_cast<int>(sizeof(d->serial)))
			d->serial[i * 2 + 1] =
			    static_cast<std::uint8_t>(w[10 + i] & 0xFF);
	}

	d->block_count = identify_block_count(buf);

	// logical sector size: word 106 bit 12 -> words 117-118, in words
	d->block_size = 512;
	if (w[106] & (1 << 12))
	{
		std::uint32_t size_words = static_cast<std::uint32_t>(w[117]) |
					   (static_cast<std::uint32_t>(w[118])
					    << 16);
		std::uint32_t bs = size_words * 2;
		if (bs >= 512 && bs <= 0x10000)
			d->block_size = bs;
	}

	// NCQ: word 76 bit 8, queue depth in word 77 bits 4:0 (0-based)
	d->ncq = false;
	d->queue_depth = 0;
	if (this->sncq && d->type == AHCI_DEV_SATA && (w[76] & (1 << 8)))
	{
		std::uint32_t depth = (w[77] & 0x1F) + 1;
		if (depth > static_cast<std::uint32_t>(this->num_slots))
			depth = static_cast<std::uint32_t>(this->num_slots);
		d->ncq = true;
		d->queue_depth = depth;
	}

	// TRIM (DSM): word 169 bit 0
	d->trim_supported = (w[169] & 1) != 0;
	d->read_only = false;
}

int Controller::atapi_capacity(device_state* d, std::uint64_t& blocks,
			       std::uint32_t& block_size)
{
	std::uint8_t cdb[12] = {ATAPI_READ_CAPACITY10};

	dma::addr buf = dma::alloc(8);
	if (!buf.virt)
		return -1;

	issue_desc desc{};
	desc.command = ATA_CMD_PACKET;
	desc.data_bytes = 8;
	desc.buf = buf;
	desc.atapi = true;
	desc.packet = cdb;
	desc.packet_len = 12;

	bool ok = this->issue_command(d->port, d->pmp, desc);
	if (!ok)
	{
		dma::free(buf, 8);
		return -1;
	}

	dma::cache_invalidate(buf.virt, 8);
	const std::uint8_t* r = static_cast<const std::uint8_t*>(buf.virt);
	std::uint64_t last_lba =
	    (static_cast<std::uint64_t>(r[0]) << 24) |
	    (static_cast<std::uint64_t>(r[1]) << 16) |
	    (static_cast<std::uint64_t>(r[2]) << 8) | r[3];
	std::uint32_t blen =
	    (static_cast<std::uint32_t>(r[4]) << 24) |
	    (static_cast<std::uint32_t>(r[5]) << 16) |
	    (static_cast<std::uint32_t>(r[6]) << 8) | r[7];

	blocks = last_lba + 1;
	block_size = blen ? blen : 2048;

	dma::free(buf, 8);
	return 0;
}

void Controller::register_device(port_state* p, int pmp, int type)
{
	device_state* d = &p->devices[pmp];
	d->present = true;
	d->port = p;
	d->pmp = pmp;
	d->type = type;
	d->block_size = 512;
	d->block_count = 0;
	d->ncq = false;
	d->queue_depth = 0;
	d->read_only = false;
	d->trim_supported = false;

	if (!this->identify(p, pmp, d, type))
	{
#ifdef DEBUG
		LOG("identify_failed; port=%d; pmp=%d; type=%d", p->port_num,
		    pmp, type);
#endif
		d->present = false;
		return;
	}

	if (type == AHCI_DEV_SATAPI)
	{
		std::uint64_t blocks = 0;
		std::uint32_t bs = 2048;
		if (this->atapi_capacity(d, blocks, bs) == 0)
		{
			d->block_count = blocks;
			d->block_size = bs;
		}
		else
		{
			d->block_count = 0;
			d->block_size = 2048;
		}
	}

	d->bdev.read = &block_read;
	d->bdev.write = &block_write;
	d->bdev.raw_read = &block_raw_read;
	d->bdev.raw_write = &block_raw_write;
	d->bdev.block_size = d->block_size;
	d->bdev.block_count = d->block_count;
	d->bdev.priv = d;

	block::device_register(&d->bdev);

#ifdef DEBUG
	LOG("block_device_registered; port=%d; pmp=%d; blocks=%llu; "
	    "size=%u; ncq=%d; trim=%d",
	    p->port_num, pmp, d->block_count, d->block_size, d->ncq ? 1 : 0,
	    d->trim_supported ? 1 : 0);
#endif
}

void Controller::register_devices(port_state* p)
{
	int type = this->check_type(p->regs);
	if (type == AHCI_DEV_PM)
	{
		// port multiplier attached: enable FBS if advertised (allows
		// per-PMP-port slot selection), else PMA and probe each PMP
		// port. absent ports simply fail IDENTIFY and are skipped.
		p->has_pmp = true;
		if (this->fbss)
		{
			p->regs->fbs |= HBA_PxFBS_EN;
			p->fbs_enabled = true;
			p->regs->cmd |= HBA_PxCMD_FBSCP;
		}
		else if (this->spm)
		{
			p->regs->cmd |= HBA_PxCMD_PMA;
		}

		for (int pmp = 0; pmp < MAX_PMP_DEVS; ++pmp)
		{
			if (p->fbs_enabled)
			{
				p->regs->fbs =
				    (p->regs->fbs &
				     ~(0xF << HBA_PxFBS_ADO_SHIFT)) |
				    (pmp << HBA_PxFBS_ADO_SHIFT);
				p->regs->fbs &= ~HBA_PxFBS_DEC;
			}
			this->register_device(p, pmp, AHCI_DEV_SATA);
		}
		return;
	}

	if (type == AHCI_DEV_SATA || type == AHCI_DEV_SATAPI)
		this->register_device(p, 0, type);
}

void Controller::handle_hotplug(port_state* p)
{
	volatile hba_port* port = p->regs;
	port->serr = static_cast<std::uint32_t>(-1);

	std::uint32_t ssts = port->ssts;
	std::uint8_t det = ssts & 0x0F;
	std::uint8_t ipm = (ssts >> 8) & 0x0F;

#ifdef DEBUG
	LOG("hotplug; port=%d; det=%u; ipm=%u; ssts=0x%x", p->port_num,
	    static_cast<std::uint64_t>(det), static_cast<std::uint64_t>(ipm),
	    static_cast<std::uint64_t>(ssts));
#endif

	if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE)
	{
		// a device is (back) on the link: rebuild the command engine
		// and register it if it is new
		this->port_rebase(p);
		int type = this->check_type(port);
		if ((type == AHCI_DEV_SATA || type == AHCI_DEV_SATAPI) &&
		    !p->devices[0].present)
			this->register_devices(p);
	}
	else
	{
		p->devices[0].present = false;
		p->has_pmp = false;
	}
}

void Controller::handle_error(port_state* p)
{
	volatile hba_port* port = p->regs;
	if (port->serr)
		port->serr = static_cast<std::uint32_t>(-1);
	this->port_recover(p);
}

bool Controller::irq(void)
{
	if (this->controller_address == 0)
		return false;

	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	std::uint32_t is = abar->is;
	if (is == 0)
		return false;

	abar->is = is;

	for (int i = 0; i < MAX_PORTS; ++i)
	{
		if (!(is & (static_cast<std::uint32_t>(1) << i)))
			continue;

		volatile hba_port* port = &abar->ports[i];
		std::uint32_t pis = port->is;
		port->is = pis;

		port_state* p = &this->ports[i];
		if (!p->present || !p->regs)
			continue;

		if (pis & HBA_PxIS_ERROR)
			p->last_error = pis;

		if (pis & HBA_PxIS_DHRS)
		{
			// D2H register FIS: non-NCQ command complete
			p->slots_done |= p->slots_issued;
		}

		if (pis & HBA_PxIS_SDBS)
		{
			// Set Device Bits FIS: NCQ completion. the SDB SActive
			// field lists the tags still active, so the in-flight
			// tags no longer listed are done.
			if (p->recv_fis.virt)
			{
				const auto* fis = reinterpret_cast<const hba_fis*>(
				    p->recv_fis.virt);
				std::uint32_t act = fis->sdbfis.protocol;
				if (fis->sdbfis.status & 0x01)
				{
					p->last_err_reg = fis->sdbfis.error;
					p->tfes = true;
				}
				std::uint32_t done = p->sact_issued & ~act;
				p->sact_issued &= act;
				p->slots_done |= done;
			}
		}

		if (pis & HBA_PxIS_TFES)
		{
			// capture the ATA error register from the received FIS
			if (p->recv_fis.virt)
			{
				const auto* fis = reinterpret_cast<const hba_fis*>(
				    p->recv_fis.virt);
				p->last_err_reg = fis->rfis.error;
			}
			p->tfes = true;
		}

		if (pis & HBA_PxIS_HOTPLUG)
			this->handle_hotplug(p);
	}
	return true;
}

port_state* Controller::port_for_device(device_state* d)
{
	return d ? d->port : nullptr;
}

device_state* Controller::find_device(int port_num)
{
	if (port_num < 0 || port_num >= MAX_PORTS)
		return nullptr;
	port_state* p = &this->ports[port_num];
	if (!p->present)
		return nullptr;
	for (int i = 0; i < MAX_PMP_DEVS; ++i)
	{
		if (p->devices[i].present)
			return &p->devices[i];
	}
	return nullptr;
}

bool Controller::read_sector(int port_num, std::uint64_t lba, void* buf)
{
	if (port_num < 0 || port_num >= MAX_PORTS)
		return false;
	port_state* p = &this->ports[port_num];
	if (!p->present)
		return false;
	device_state* d = &p->devices[0];
	if (!d->present)
		return false;
	return this->read(d, lba, 1, buf);
}

}

}

}
