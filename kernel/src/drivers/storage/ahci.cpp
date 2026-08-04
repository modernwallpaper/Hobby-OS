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

Controller controller;

// per-slot command tables are laid out 256 bytes apart; 64 (cfis) + 16 (acmd)
// + 48 (rsv) leaves room for 8 PRDT entries of 16 bytes each
static constexpr int MAX_PRDT = 8;
// each PRDT entry can describe up to 4MB (dbc is 22 bits, byte count minus 1)
static constexpr std::uint64_t PRDT_MAX_BYTES = 0x400000;
// the sector count in an LBA48 H2D FIS is 16 bits
static constexpr std::uint32_t MAX_SECTORS = 0xFFFF;
static constexpr std::uint64_t CMD_TIMEOUT_US = 5000000;

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
	auto* p = static_cast<port_state*>(dev->priv);
	if (!p || !p->ahci)
		return false;
	return p->ahci->read(p, lba, count, buf);
}

static bool block_write(block::device* dev, std::uint64_t lba,
			std::uint32_t count, const void* buf)
{
	auto* p = static_cast<port_state*>(dev->priv);
	if (!p || !p->ahci)
		return false;
	return p->ahci->write(p, lba, count, buf);
}

void ahci_irq_stub(interrupts::idt::frame* frame)
{
	(void)frame;
	controller.irq();
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

void Controller::init(void)
{
	this->find_controller();

	this->irq_vector = DEVICE_IRQ_VECTOR;

	// MSI first (device-supplied), falling back to INTx on the IOAPIC
	this->use_msi = this->msi_enable();
	if (this->use_msi)
	{
		interrupts::idt::register_irq_handler(this->irq_vector,
						      ahci_irq_stub);
	}
	else
	{
		this->intx_setup();
	}

	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	this->bohc_handoff(abar);

	this->reset_controller(abar);

	abar->ghc |= (1 << 31);	 // Turn on AHCI mode
	abar->ghc |= HBA_GHC_IE; // Enable global interrupts

	this->probe_port(abar);

	int port_count = (abar->cap & 0x1F) + 1;
	std::uint32_t pi = abar->pi;
	for (int i = 0; i < port_count; ++i)
	{
		if (pi & 1)
		{
			port_state* p = &this->ports[i];
			p->present = true;
			p->port_num = i;
			p->regs = &abar->ports[i];
			p->ahci = this;
			p->slots_issued = 0;
			p->slots_done = 0;
			p->tfes = false;

			this->port_rebase(p);

			if (this->check_type(p->regs) == AHCI_DEV_SATA)
				this->register_device(p);
		}
		pi >>= 1;
	}

	abar->ghc |= HBA_GHC_IE; // GHC.IE: global interrupt enable
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

#ifdef DEBUG
	LOG("success=true;");
#endif
}

// returns true if this controller generated the interrupt. on a shared line
// other devices registered on the same vector are chained after this handler,
// so a zero global status simply means "not ours" and they still get serviced.
bool Controller::irq(void)
{
	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	std::uint32_t is = abar->is;
	if (is == 0)
		return false;

	abar->is = is;

	for (int i = 0; i < MAX_PORTS; ++i)
	{
		if (!(is & (1 << i)))
			continue;

		volatile hba_port* port = &abar->ports[i];
		std::uint32_t pis = port->is;
		port->is = pis;

		port_state* p = &this->ports[i];
		if (!p->present)
			continue;

		if (pis & HBA_PxIS_TFES)
		{
#ifdef DEBUG
			LOG("port=%d; task_file_error; pis=0x%x", i, pis);
#endif
			p->tfes = true;
		}
		if (pis & HBA_PxIS_D2H)
		{
			p->slots_done |= p->slots_issued;
		}
	}
	return true;
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
	LOG("msi_enabled; cap=0x%x; addr=0x%08x; data=0x%08x; dest=%u",
	    static_cast<std::uint64_t>(cap),
	    static_cast<std::uint64_t>(msg_addr),
	    static_cast<std::uint64_t>(msg_data),
	    static_cast<std::uint64_t>(dest));
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

void Controller::find_controller(void)
{
	int dev_count = pci::pci.device_count();
	for (int i = 0; i < dev_count; ++i)
	{
		auto device = pci::pci.get_device(i);
		if (!device)
			continue;
		if (device->class_code == this->AHCI_CLASS)
		{
			if (device->subclass == this->AHCI_ATA_SUBCLASS)
			{
				if (device->prog_if != this->AHCI_PROG_IF)
					continue;

				this->controller_mode = Controller::MODE::ATA;

				this->pci_bus = device->bus;
				this->pci_slot = device->slot;
				this->pci_func = device->func;

				// enable bus master + memory space in the PCI
				// command register (AHCI is memory mapped, no
				// IO space needed)
				std::uint32_t cmd = pci::pci.read_config(
				    device->bus, device->slot, device->func,
				    PCI_COMMAND);
				cmd |= 0x6; // mem space, bus master
				pci::pci.write_config(device->bus, device->slot,
						      device->func, PCI_COMMAND,
						      cmd);

				pci::bar_info bar = pci::pci.read_bar_info(
				    device->bus, device->slot, device->func, 5);
				if (bar.is_io || bar.address == 0 ||
				    bar.size < 0x100)
				{
					PANIC("AHCI ABAR is unusable");
				}

				this->controller_address = bar.address;
				this->controller_size = bar.size;

				memory::map_mmio_range(bar.address,
						       bar.address +
							   memory::hhdm_offset,
						       bar.size);

				std::uint32_t int_line = pci::pci.read_config(
				    device->bus, device->slot, device->func,
				    PCI_INTERRUPT_LINE);
				this->irq_line = int_line & 0xFF;
#ifdef DEBUG
				LOG("ahci_controller_found; bus=%02x; "
				    "slot=%02x; "
				    "func=%x; abar=%p; size=0x%x; irq=%u",
				    static_cast<std::uint64_t>(this->pci_bus),
				    static_cast<std::uint64_t>(this->pci_slot),
				    static_cast<std::uint64_t>(this->pci_func),
				    static_cast<std::uintptr_t>(
					this->controller_address),
				    static_cast<std::uint64_t>(
					this->controller_size),
				    static_cast<std::uint64_t>(this->irq_line));
#endif
				return;
			}
			else if (device->subclass == this->AHCI_IDE_SUBCLASS)
			{
				this->controller_mode = Controller::MODE::IDE;

				this->pci_bus = device->bus;
				this->pci_slot = device->slot;
				this->pci_func = device->func;

				std::uint32_t cmd = pci::pci.read_config(
				    device->bus, device->slot, device->func,
				    PCI_COMMAND);

				cmd |= 0x5; // IO space + bus master
				pci::pci.write_config(device->bus, device->slot,
						      device->func, PCI_COMMAND,
						      cmd);

				std::uint8_t prog_if = device->prog_if;

				bool primary_native = prog_if & 0x01;
				bool secondary_native = prog_if & 0x04;

				if (primary_native)
				{
					this->primary_io_base =
					    pci::pci.read_bar(device->bus,
							      device->slot,
							      device->func, 0) &
					    ~0x3;
					this->primary_ctrl_base =
					    pci::pci.read_bar(device->bus,
							      device->slot,
							      device->func, 1) &
					    ~0x3;
					this->primary_ctrl_base += 2;
				}
				else
				{
					this->primary_io_base = 0x1F0;
					this->primary_ctrl_base = 0x3F6;
				}

				if (secondary_native)
				{
					this->secondary_io_base =
					    pci::pci.read_bar(device->bus,
							      device->slot,
							      device->func, 2) &
					    ~0x3;
					this->secondary_ctrl_base =
					    pci::pci.read_bar(device->bus,
							      device->slot,
							      device->func, 3) &
					    ~0x3;
					this->secondary_ctrl_base += 2;
				}
				else
				{
					this->secondary_io_base = 0x170;
					this->secondary_ctrl_base = 0x376;
				}

				this->bus_master_base =
				    pci::pci.read_bar(device->bus, device->slot,
						      device->func, 4) &
				    ~0x3;

				LOG("ide_controller_found; bus=%02x; "
				    "slot=%02x; "
				    "func=%x; primary_io=%p; primary_ctrl=%p; "
				    "secondary_io=%p; secondary_ctrl=%p; "
				    "bmide=%p",
				    static_cast<std::uint64_t>(this->pci_bus),
				    static_cast<std::uint64_t>(this->pci_slot),
				    static_cast<std::uint64_t>(this->pci_func),
				    static_cast<std::uintptr_t>(
					this->primary_io_base),
				    static_cast<std::uintptr_t>(
					this->primary_ctrl_base),
				    static_cast<std::uintptr_t>(
					this->secondary_io_base),
				    static_cast<std::uintptr_t>(
					this->secondary_ctrl_base),
				    static_cast<std::uintptr_t>(
					this->bus_master_base));
				return;
			}
		}
	}

	PANIC("Could not find AHCI or IDE controller");
}

void Controller::bohc_handoff(volatile hba_mem* abar)
{
	if (!(abar->bohc & 1))
	{
#ifdef DEBUG
		LOG("bohc_handoff_success");
#endif
		return;
	}

	abar->bohc |= 2;

	if (!wait_reg_clear(abar->bohc, 1, CONTROLLER_TIMEOUT_US))
	{
#ifdef DEBUG
		LOG("bohc_handoff_timeout; bios_did_not_release");
#endif
	}

	if (abar->bohc & 8)
		abar->bohc |= 8;
#ifdef DEBUG
	LOG("bohc_handoff_success");
#endif
}

void Controller::probe_port(volatile hba_mem* abar)
{
	// search disk in implemented ports
	std::uint32_t pi = abar->pi;
	for (int i = 0; i < 32; ++i)
	{
		if (pi & 1)
		{
			int dt = this->check_type(&abar->ports[i]);
			if (dt == AHCI_DEV_SATA)
			{
#ifdef DEBUG
				LOG("sata_drive_port=%d", i);
#endif
			}
			else if (dt == AHCI_DEV_SATAPI)
			{
#ifdef DEBUG
				LOG("satapi_drive_port=%d", i);
#endif
			}
			else if (dt == AHCI_DEV_SEMB)
			{
#ifdef DEBUG
				LOG("semb_drive_port=%d", i);
#endif
			}
			else if (dt == AHCI_DEV_PM)
			{
#ifdef DEBUG
				LOG("pm_drive_port=%d", i);
#endif
			}
			else
			{
#ifdef DEBUG
				LOG("no_drive_found_at_port=%d", i);
#endif
			}
		}

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
	for (int i = 0; i < 32; ++i)
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

	this->start_cmd(port);

	port->is =
	    static_cast<std::uint32_t>(-1); // clear pending port interrupts
	port->ie = HBA_PxIS_D2H | HBA_PxIS_TFES; // enable D2H + TFES
	port->cmd |= HBA_PxCMD_IE;		 // enable port interrupts

#ifdef DEBUG
	LOG("port=%i; clb=%p; fb=%p; ct=%p", p->port_num,
	    static_cast<std::uintptr_t>(p->cmd_list.phys),
	    static_cast<std::uintptr_t>(p->recv_fis.phys),
	    static_cast<std::uintptr_t>(p->cmd_tbl.phys));
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
	for (int i = 0; i < 32; ++i)
	{
		if (!(p->slots_issued & (static_cast<std::uint32_t>(1) << i)))
		{
			p->slots_issued |= static_cast<std::uint32_t>(1) << i;
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
	while (!(p->slots_done & mask))
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

	this->start_cmd(port);

	p->slots_issued = 0;
	p->slots_done = 0;
	p->tfes = false;
	return true;
}

bool Controller::issue_command(port_state* p, std::uint64_t lba,
			       std::uint32_t sectors, dma::addr dma_buf,
			       std::uint8_t command, bool to_device)
{
	volatile hba_port* port = p->regs;

	p->lock.lock();

	int slot = this->alloc_slot(p);
	if (slot < 0)
	{
		p->lock.unlock();
		return false;
	}

	std::uint32_t mask = static_cast<std::uint32_t>(1) << slot;

	auto* header = static_cast<hba_cmd_header*>(p->cmd_list.virt) + slot;
	auto* tbl = reinterpret_cast<hba_cmd_tbl*>(
	    static_cast<std::uint8_t*>(p->cmd_tbl.virt) + slot * 256);

	memory::memset(tbl, 0, sizeof(hba_cmd_tbl));

	// PRDT: split the (contiguous) DMA buffer into up to 4MB entries
	std::uint64_t dbc_phys = dma_buf.phys;
	std::size_t expected_bytes = static_cast<std::size_t>(sectors) * 512;
	std::size_t left = expected_bytes;
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
	if (nprdt == 0)
	{
		p->lock.unlock();
		return false;
	}
	tbl->prdt_entry[nprdt - 1].i = 1; // interrupt on last entry

	// H2D FIS: LBA48 command
	auto* fis = reinterpret_cast<reg_h2d*>(tbl->cfis);
	fis->fis_type = static_cast<std::uint8_t>(fis_type::FIS_TYPE_REG_H2D);
	fis->c = 1;
	fis->command = command;
	fis->lba0 = static_cast<std::uint8_t>(lba);
	fis->lba1 = static_cast<std::uint8_t>(lba >> 8);
	fis->lba2 = static_cast<std::uint8_t>(lba >> 16);
	fis->lba3 = static_cast<std::uint8_t>(lba >> 24);
	fis->lba4 = static_cast<std::uint8_t>(lba >> 32);
	fis->lba5 = static_cast<std::uint8_t>(lba >> 40);
	fis->device = 1 << 6; // LBA addressing
	fis->countl = static_cast<std::uint8_t>(sectors);
	fis->counth = static_cast<std::uint8_t>(sectors >> 8);

	header->cfl = sizeof(reg_h2d) / 4;
	header->w = to_device ? 1 : 0; // host-to-device for writes
	header->prdtl = static_cast<std::uint16_t>(nprdt);
	header->pmp = 0;

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

		port->ci = mask;

		p->lock.unlock();

		bool ok = this->wait_slot(p, mask, CMD_TIMEOUT_US);
		if (!ok)
		{
#ifdef DEBUG
			LOG("cmd_timeout; port=%d; slot=%d; cmd=0x%x; "
			    "attempt=%d",
			    p->port_num, slot,
			    static_cast<std::uint64_t>(command), attempt);
#endif
		}
		else if (p->tfes || (port->tfd & 0x1)) // PxTFD bit 0 = ERR
		{
#ifdef DEBUG
			LOG("cmd_error; port=%d; slot=%d; cmd=0x%x; tfd=0x%x; "
			    "serr=0x%x; attempt=%d",
			    p->port_num, slot,
			    static_cast<std::uint64_t>(command),
			    static_cast<std::uint64_t>(port->tfd),
			    static_cast<std::uint64_t>(port->serr), attempt);
#endif
			ok = false;
		}
		else
		{
			// the controller wrote the PRD byte count into the
			// command header; read it fresh and require it to match
			// the expected transfer size
			dma::cache_invalidate(header, sizeof(hba_cmd_header));
			if (header->prdbc != expected_bytes)
			{
#ifdef DEBUG
				LOG("prd_byte_count_mismatch; port=%d; "
				    "slot=%d; "
				    "expected=%u; prdbc=%u; attempt=%d",
				    p->port_num, slot,
				    static_cast<std::uint64_t>(expected_bytes),
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

bool Controller::transfer(port_state* p, std::uint64_t lba,
			  std::uint32_t sectors, void* buf, bool to_device)
{
	volatile hba_port* port = p->regs;
	if (!p->present || this->check_type(port) != AHCI_DEV_SATA)
		return false;
	if (sectors == 0)
		return true;
	if (p->block_count != 0 && lba + sectors > p->block_count)
		return false;

	// the DMA buffer must be physically contiguous; the caller's buffer is
	// kernel virtual memory that need not be, so copy through a dma buffer
	std::size_t total_bytes = static_cast<std::size_t>(sectors) * 512;
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

	std::uint64_t remaining = sectors;
	std::uint64_t cur_lba = lba;
	std::uint64_t offset = 0;
	bool ok = true;

	while (remaining > 0)
	{
		std::uint32_t chunk =
		    remaining > MAX_SECTORS
			? MAX_SECTORS
			: static_cast<std::uint32_t>(remaining);

		dma::addr chunk_buf;
		chunk_buf.virt =
		    static_cast<std::uint8_t*>(dma_buf.virt) + offset;
		chunk_buf.phys = dma_buf.phys + offset;

		ok = this->issue_command(p, cur_lba, chunk, chunk_buf,
					 to_device ? ATA_CMD_WRITE_DMA_EXT
						   : ATA_CMD_READ_DMA_EXT,
					 to_device);
		if (!ok)
			break;

		remaining -= chunk;
		cur_lba += chunk;
		offset += static_cast<std::uint64_t>(chunk) * 512;
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

bool Controller::read(port_state* p, std::uint64_t lba, std::uint32_t count,
		      void* buf)
{
	return this->transfer(p, lba, count, buf, false);
}

bool Controller::write(port_state* p, std::uint64_t lba, std::uint32_t count,
		       const void* buf)
{
	return this->transfer(p, lba, count, const_cast<void*>(buf), true);
}

bool Controller::identify(port_state* p, std::uint8_t* out)
{
	dma::addr buf = dma::alloc(512);
	if (!buf.virt)
		return false;

	bool ok =
	    this->issue_command(p, 0, 1, buf, ATA_CMD_IDENTIFY_DEVICE, false);
	if (ok)
	{
		dma::cache_invalidate(buf.virt, 512);
		memory::memcpy(out, buf.virt, 512);
	}

	dma::free(buf, 512);
	return ok;
}

void Controller::register_device(port_state* p)
{
	std::uint8_t id[512];
	if (!this->identify(p, id))
	{
#ifdef DEBUG
		LOG("identify_failed; port=%d", p->port_num);
#endif
		return;
	}

	p->block_size = 512;
	p->block_count = identify_block_count(id);

	p->bdev.raw_read = &block_read;
	p->bdev.raw_write = &block_write;
	p->bdev.block_size = p->block_size;
	p->bdev.block_count = p->block_count;
	p->bdev.priv = p;

	block::device_register(&p->bdev);

#ifdef DEBUG
	LOG("block_device_registered; port=%d; blocks=%lu; size=%u",
	    p->port_num, p->block_count, p->block_size);
#endif
}

bool Controller::read_sector(int port_num, std::uint64_t lba, void* buf)
{
	if (port_num < 0 || port_num >= MAX_PORTS)
		return false;
	port_state* p = &this->ports[port_num];
	if (!p->present)
		return false;
	return this->read(p, lba, 1, buf);
}

} // namespace ahci

} // namespace storage

} // namespace drivers
