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
		if (timers::hpet::hpet.read_counter() - start >= timeout_us * ticks)
			return false;
		asm volatile("pause" : : : "memory");
	}
	return true;
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

	abar->ghc |= (1 << 31); // Turn on AHCI mode
	abar->ghc |= HBA_GHC_IE; // Enable global interrupts

	this->probe_port(abar);

	int port_count = (abar->cap & 0x1F) + 1;
	std::uint32_t pi = abar->pi;
	for (int i = 0; i < port_count; ++i)
	{
		if (pi & 1)
			this->port_rebase(&abar->ports[i], i);
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

void Controller::irq(void)
{
	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	std::uint32_t is = abar->is;
	if (is == 0)
		return;

	abar->is = is;

	for (int i = 0; i < 32; ++i)
	{
		if (!(is & (1 << i)))
			continue;

		volatile hba_port* port = &abar->ports[i];
		std::uint32_t pis = port->is;
		port->is = pis;

		this->irq_fired = true;

		if (pis & HBA_PxIS_TFES)
		{
#ifdef DEBUG
			LOG("port=%d; task_file_error", i);
#endif
		}
		if (pis & HBA_PxIS_D2H)
		{
#ifdef DEBUG
			LOG("port=%d; d2h", i);
#endif
		}
	}
}

// program the MSI capability: single edge-triggered message routed to the
// boot CPU. returns true only if the capability was found and the enable bit
// stuck.
bool Controller::msi_enable(void)
{
	std::uint8_t cap = pci::pci.find_capability(this->pci_bus,
						    this->pci_slot,
						    this->pci_func,
						    PCI_CAP_MSI);
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
		msg_addr = 0xFEE00000 | (static_cast<std::uint32_t>(dest) << 12);
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
	std::uint32_t gsi = acpi::acpi.resolve_irq(
	    static_cast<std::uint8_t>(this->irq_line));
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

	std::uint8_t pin = static_cast<std::uint8_t>(
	    gsi - acpi::acpi.ioapic_gsi_base);

	interrupts::idt::register_irq_handler(this->irq_vector, ahci_irq_stub);

	interrupts::ioapic::ioapic.redirect_irq(
	    pin, this->irq_vector, interrupts::apic::apic.get_id(),
	    true /* level */, true /* active low */);
	interrupts::ioapic::ioapic.unmask_irq(pin);

#ifdef DEBUG
	LOG("intx_routed; irq_line=%u; gsi=%u; pin=%u; vector=0x%x",
	    static_cast<std::uint64_t>(this->irq_line),
	    static_cast<std::uint64_t>(gsi),
	    static_cast<std::uint64_t>(pin),
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

				memory::map_mmio_range(
				    bar.address,
				    bar.address + memory::hhdm_offset,
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

void Controller::port_rebase(volatile hba_port* port, int port_number)
{
	(void)port_number;
	this->stop_cmd(port);

	// Allocate command list (1K = 32 entries * 32 bytes)
	std::uint64_t clb_phys = memory::buddy.alloc_pages(0);
	void* clb_virt = memory::phys_to_virt(clb_phys);
	port->clb = static_cast<std::uint32_t>(clb_phys);
	port->clbu = static_cast<std::uint32_t>(clb_phys >> 32);
	memory::memset(clb_virt, 0, 1024);

	// Allocate received FIS (256 bytes)
	std::uint64_t fb_phys = memory::buddy.alloc_pages(0);
	void* fb_virt = memory::phys_to_virt(fb_phys);
	port->fb = static_cast<std::uint32_t>(fb_phys);
	port->fbu = static_cast<std::uint32_t>(fb_phys >> 32);
	memory::memset(fb_virt, 0, 256);

	// Allocate command tables (8K = 32 entries * 256 bytes each)
	std::uint64_t ct_phys = memory::buddy.alloc_pages(1);
	void* ct_virt = memory::phys_to_virt(ct_phys);

	auto* cmd_header = static_cast<hba_cmd_header*>(clb_virt);
	for (int i = 0; i < 32; ++i)
	{
		cmd_header[i].prdtl = 8;
		std::uint64_t entry_phys = ct_phys + i * 256;
		cmd_header[i].ctba = static_cast<std::uint32_t>(entry_phys);
		cmd_header[i].ctbau =
		    static_cast<std::uint32_t>(entry_phys >> 32);
		memory::memset(static_cast<std::uint8_t*>(ct_virt) + i * 256, 0,
			       256);
	}

	dma_barrier();

	this->start_cmd(port);

	port->is = (std::uint32_t)-1; // clear pending port interrupts
	port->ie = HBA_PxIS_D2H | HBA_PxIS_TFES; // enable D2H + TFES
	port->cmd |= HBA_PxCMD_IE; // enable port interrupts

#ifdef DEBUG
	LOG("port=%i; clb=%p; fb=%p; ct=%p", port_number,
	    static_cast<std::uintptr_t>(clb_phys),
	    static_cast<std::uintptr_t>(fb_phys),
	    static_cast<std::uintptr_t>(ct_phys));
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

bool Controller::read_sector(int port_num, std::uint64_t lba, void* buf)
{
	auto* abar = static_cast<volatile hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	int port_count = (abar->cap & 0x1F) + 1;
	if (port_num < 0 || port_num >= port_count)
		return false;

	volatile hba_port* port = &abar->ports[port_num];
	if (this->check_type(port) != AHCI_DEV_SATA)
		return false;

	constexpr int slot = 0;

	// DMA target lives in physical memory; the controller writes it, the
	// CPU reads it after the completion interrupt
	dma::addr dma_buf = dma::alloc(512);
	if (!dma_buf.virt)
		return false;

	std::uint64_t clb_phys = static_cast<std::uint64_t>(port->clb) |
				 (static_cast<std::uint64_t>(port->clbu) << 32);
	auto* header =
	    static_cast<hba_cmd_header*>(memory::phys_to_virt(clb_phys)) + slot;

	std::uint64_t ct_phys = static_cast<std::uint64_t>(header->ctba) |
				(static_cast<std::uint64_t>(header->ctbau) << 32);
	auto* tbl = static_cast<hba_cmd_tbl*>(memory::phys_to_virt(ct_phys));

	memory::memset(tbl, 0, sizeof(hba_cmd_tbl));

	// PRDT: one entry covering a single 512-byte sector
	tbl->prdt_entry[0].dba = static_cast<std::uint32_t>(dma_buf.phys);
	tbl->prdt_entry[0].dbau = static_cast<std::uint32_t>(dma_buf.phys >> 32);
	tbl->prdt_entry[0].dbc = 512 - 1;
	tbl->prdt_entry[0].i = 1;

	// H2D FIS: READ_DMA_EXT (LBA48), 48-bit sector number
	auto* fis = reinterpret_cast<reg_h2d*>(tbl->cfis);
	fis->fis_type = static_cast<std::uint8_t>(fis_type::FIS_TYPE_REG_H2D);
	fis->c = 1;
	fis->command = ATA_CMD_READ_DMA_EXT;
	fis->lba0 = static_cast<std::uint8_t>(lba);
	fis->lba1 = static_cast<std::uint8_t>(lba >> 8);
	fis->lba2 = static_cast<std::uint8_t>(lba >> 16);
	fis->lba3 = static_cast<std::uint8_t>(lba >> 24);
	fis->lba4 = static_cast<std::uint8_t>(lba >> 32);
	fis->lba5 = static_cast<std::uint8_t>(lba >> 40);
	fis->device = 1 << 6; // LBA addressing
	fis->countl = 1;
	fis->counth = 0;

	header->cfl = sizeof(reg_h2d) / 4;
	header->w = 0; // host-to-device
	header->prdtl = 1;
	header->pmp = 0;

	// clear pending port status, then issue the command
	port->is = static_cast<std::uint32_t>(-1);
	this->irq_fired = false;

	dma_barrier();

	port->ci = 0;
	dma_barrier();
	port->ci = static_cast<std::uint32_t>(1) << slot;

	// wait for the completion interrupt to fire (this is the point of the
	// exercise: the read only succeeds if MSI/INTx delivery reaches the ISR)
	std::uint64_t timeout_us = 5000000;
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	std::uint64_t ticks = freq / 1000000;
	if (ticks == 0)
		ticks = 1;
	std::uint64_t start = timers::hpet::hpet.read_counter();
	while (!this->irq_fired)
	{
		if (timers::hpet::hpet.read_counter() - start >= timeout_us * ticks)
			break;
		asm volatile("pause" : : : "memory");
	}

	dma_barrier();

	std::uint32_t tfd = port->tfd;
	bool ok = this->irq_fired && !(tfd & 0x1); // tfd bit 0 = ERR

	if (ok)
		memory::memcpy(buf, dma_buf.virt, 512);
#ifdef DEBUG
	else
		LOG("read_failed; port=%d; tfd=0x%x; pis=0x%x; irq=%u", port_num,
		    tfd, port->is, static_cast<unsigned>(this->irq_fired));
#endif

	dma::free(dma_buf, 512);
	return ok;
}

} // namespace ahci

} // namespace storage

} // namespace drivers
