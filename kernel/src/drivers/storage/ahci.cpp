#include <drivers/storage/ahci.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <memory/paging.hpp>
#include <panic/panic.hpp>
#include <pci/pci.hpp>

// TODOS: reset controller

namespace drivers
{

namespace storage
{

namespace ahci
{

Controller controller;

void Controller::init(void)
{
	this->find_controller();

	memory::map_mmio_page(this->controller_address,
			      this->controller_address + memory::hhdm_offset);

	auto* abar = static_cast<hba_mem*>(
	    memory::phys_to_virt(this->controller_address));

	this->probe_port(abar);

	int port_count = (abar->cap & 0x1F) + 1;
	std::uint32_t pi = abar->pi;
	for (int i = 0; i < port_count; ++i)
	{
		if (pi & 1)
			this->handoff_bios_port(&abar->ports[i], i);
		pi >>= 1;
	}
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

				// enable bus master + memory + IO in PCI
				// command reg
				std::uint32_t cmd = pci::pci.read_config(
				    device->bus, device->slot, device->func,
				    PCI_COMMAND);
				cmd |= 0x7; // IO, mem, bud master
				pci::pci.write_config(device->bus, device->slot,
						      device->func, PCI_COMMAND,
						      cmd);

				std::uint32_t bar = pci::pci.read_bar(
				    device->bus, device->slot, device->func, 5);
				std::uint64_t addr = bar & ~0xFULL;
				if ((bar & 0x06) == 0x04)
				{
					std::uint32_t hi = pci::pci.read_config(
					    device->bus, device->slot,
					    device->func, 0x28);
					addr |= static_cast<std::uint64_t>(hi)
						<< 32;
				}
				std::uint32_t int_line = pci::pci.read_config(
				    device->bus, device->slot, device->func,
				    PCI_INTERRUPT_LINE);
				this->irq_line = int_line & 0xFF;

				if (addr == 0)
				{
					PANIC("AHCI ABAR is zero");
				}

				this->controller_address = addr;
#ifdef DEBUG
				LOG("ahci_controller_found; bus=%02x; "
				    "slot=%02x; "
				    "func=%x; abar=%p; irq=%u",
				    static_cast<std::uint64_t>(this->pci_bus),
				    static_cast<std::uint64_t>(this->pci_slot),
				    static_cast<std::uint64_t>(this->pci_func),
				    static_cast<std::uintptr_t>(
					this->controller_address),
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

void Controller::probe_port(hba_mem* abar)
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

int Controller::check_type(hba_port* port)
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

void Controller::handoff_bios_port(hba_port* port, int port_number)
{
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

	this->start_cmd(port);
#ifdef DEBUG
	LOG("port=%i; clb=%p; fb=%p; ct=%p", port_number,
	    static_cast<std::uintptr_t>(clb_phys),
	    static_cast<std::uintptr_t>(fb_phys),
	    static_cast<std::uintptr_t>(ct_phys));
#endif
}

void Controller::start_cmd(hba_port* port)
{
	// wait until CR (bit 15) is cleared
	while (port->cmd & HBA_PxCMD_CR)
		;
	// set FRE (bit 4) and ST (bit 0)
	port->cmd |= HBA_PxCMD_FRE;
	port->cmd |= HBA_PxCMD_ST;
}

void Controller::stop_cmd(hba_port* port)
{
	// clear ST (bit 0)
	port->cmd &= ~HBA_PxCMD_ST;
	// clear FRE (bit 4)
	port->cmd &= ~HBA_PxCMD_FRE;

	// wait until (bit 14) and CR (bit 15) are cleared
	for (;;)
	{
		if (port->cmd & HBA_PxCMD_FR || port->cmd & HBA_PxCMD_CR)
			continue;
		break;
	}
}

} // namespace ahci

} // namespace storage

} // namespace drivers
