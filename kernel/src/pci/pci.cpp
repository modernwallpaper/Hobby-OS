#include <logging/logger.hpp>
#include <panic/panic.hpp>
#include <pci/pci.hpp>
#include <ports/ports.hpp>

namespace pci
{

Pci pci;

void Pci::init(void)
{
	this->num_devices = 0;
	this->enumerate();
}

std::uint32_t Pci::read_config(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func, std::uint8_t offset)
{
	std::uint32_t address = static_cast<std::uint32_t>(
	    (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) |
	    0x80000000);
	ports::outl(PCI_CONFIG_ADDR, address);
	return ports::inl(PCI_CONFIG_DATA);
}

void Pci::write_config(std::uint8_t bus, std::uint8_t slot, std::uint8_t func,
		       std::uint8_t offset, std::uint32_t value)
{
	std::uint32_t address = static_cast<std::uint32_t>(
	    (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) |
	    0x80000000);
	ports::outl(PCI_CONFIG_ADDR, address);
	ports::outl(PCI_CONFIG_DATA, value);
}

std::uint16_t Pci::read_vendor(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func)
{
	return static_cast<std::uint16_t>(
	    this->read_config(bus, slot, func, PCI_VENDOR_ID) & 0xFFFF);
}

std::uint16_t Pci::read_device_id(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func)
{
	return static_cast<std::uint16_t>(
	    this->read_config(bus, slot, func, PCI_DEVICE_ID) >> 16);
}

std::uint8_t Pci::read_class(std::uint8_t bus, std::uint8_t slot,
			     std::uint8_t func)
{
	return static_cast<std::uint8_t>(
	    this->read_config(bus, slot, func, PCI_CLASS) >> 24);
}

std::uint8_t Pci::read_subclass(std::uint8_t bus, std::uint8_t slot,
				std::uint8_t func)
{
	return static_cast<std::uint8_t>(
	    this->read_config(bus, slot, func, PCI_SUBCLASS) >> 16);
}

std::uint8_t Pci::read_prog_if(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func)
{
	return static_cast<std::uint8_t>(
	    this->read_config(bus, slot, func, PCI_PROG_IF) >> 8);
}

std::uint8_t Pci::read_header_type(std::uint8_t bus, std::uint8_t slot,
				   std::uint8_t func)
{
	return static_cast<std::uint8_t>(
	    this->read_config(bus, slot, func, PCI_HEADER_TYPE) >> 16);
}

std::uint32_t Pci::read_bar(std::uint8_t bus, std::uint8_t slot,
			    std::uint8_t func, int bar)
{
	if (bar < 0 || bar > 5)
		return 0;
	return this->read_config(bus, slot, func, PCI_BAR0 + bar * 4);
}

// 16-bit config read/write that preserves the untouched half of the dword
std::uint16_t Pci::read_config16(std::uint8_t bus, std::uint8_t slot,
				 std::uint8_t func, std::uint8_t offset)
{
	std::uint8_t aligned = offset & ~0x3;
	std::uint32_t raw = this->read_config(bus, slot, func, aligned);
	return static_cast<std::uint16_t>(raw >> ((offset & 0x3) * 8));
}

void Pci::write_config16(std::uint8_t bus, std::uint8_t slot,
			 std::uint8_t func, std::uint8_t offset,
			 std::uint16_t value)
{
	std::uint8_t aligned = offset & ~0x3;
	std::uint32_t raw = this->read_config(bus, slot, func, aligned);
	std::uint8_t shift = (offset & 0x3) * 8;
	raw &= ~(0xFFFF << shift);
	raw |= static_cast<std::uint32_t>(value) << shift;
	this->write_config(bus, slot, func, aligned, raw);
}

// walk the capability linked list looking for cap_id; returns its config
// offset, or 0 if the device does not implement it
std::uint8_t Pci::find_capability(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func, std::uint8_t cap_id)
{
	std::uint8_t cap = static_cast<std::uint8_t>(
	    this->read_config(bus, slot, func, PCI_CAP_PTR));

	while ((cap & 0xFC) != 0)
	{
		std::uint16_t id_next =
		    this->read_config16(bus, slot, func, cap);
		if (static_cast<std::uint8_t>(id_next & 0xFF) == cap_id)
			return cap;
		cap = static_cast<std::uint8_t>(id_next >> 8);
	}
	return 0;
}

// decode a BAR: full 64-bit address plus its size, obtained by writing all
// ones and reading back the mask (the original value is restored)
bar_info Pci::read_bar_info(std::uint8_t bus, std::uint8_t slot,
			    std::uint8_t func, int bar)
{
	bar_info info = {0, 0, false, false, false};
	if (bar < 0 || bar > 5)
		return info;

	std::uint8_t offset = PCI_BAR0 + bar * 4;
	std::uint32_t orig = this->read_config(bus, slot, func, offset);

	info.is_io = (orig & PCI_BAR_TYPE_IO) != 0;
	info.is_64 = (orig & PCI_BAR_64_BIT) == PCI_BAR_64_BIT;
	info.prefetchable = (orig & PCI_BAR_PREFETCH) != 0;

	std::uint32_t mask = info.is_io ? 0xFFFFFFFC : 0xFFFFFFF0;
	std::uint64_t addr = orig & mask;
	if (info.is_64 && bar < 5)
	{
		std::uint32_t hi = this->read_config(bus, slot, func,
						     offset + 4);
		addr |= static_cast<std::uint64_t>(hi) << 32;
	}
	info.address = addr;

	if (info.is_64 && bar < 5)
	{
		std::uint32_t orig_hi =
		    this->read_config(bus, slot, func, offset + 4);
		this->write_config(bus, slot, func, offset, 0xFFFFFFFF);
		this->write_config(bus, slot, func, offset + 4, 0xFFFFFFFF);
		std::uint32_t probe_lo =
		    this->read_config(bus, slot, func, offset);
		std::uint32_t probe_hi =
		    this->read_config(bus, slot, func, offset + 4);
		this->write_config(bus, slot, func, offset + 4, orig_hi);
		this->write_config(bus, slot, func, offset, orig);

		std::uint64_t raw = static_cast<std::uint64_t>(probe_hi) << 32 |
				    probe_lo;
		info.size = ~(raw & ~0xFULL) + 1;
	}
	else
	{
		this->write_config(bus, slot, func, offset, 0xFFFFFFFF);
		std::uint32_t probe =
		    this->read_config(bus, slot, func, offset);
		this->write_config(bus, slot, func, offset, orig);

		if (info.is_io)
			info.size = ~(probe & 0xFFFFFFFC) + 1;
		else
			info.size = ~(probe & 0xFFFFFFF0) + 1;
	}

	// a BAR that reports no address (or a read-only/unimplemented BAR)
	// is unusable regardless of what the size probe returned
	if (info.size == 0 || info.address == 0)
		info.size = 0;

	return info;
}

const device* Pci::get_device(int index) const
{
	if (index < 0 || index >= this->num_devices)
		return nullptr;
	return &this->devices[index];
}

void Pci::enumerate(void)
{
#ifdef DEBUG
	if (this->read_vendor(0, 0, 0) == 0xFFFF)
	{
		LOG("pci_host_controller_not_found");
	}
#endif

	this->check_bus(0);

#ifdef DEBUG
	for (int i = 0; i < num_devices; ++i)
	{
		LOG("bus=%02x; slot=%02x.%x; func=%04x; vendor_id=%04x; "
		    "class=%02x; subclass=%02x; class_name=%s",
		    this->devices[i].bus, this->devices[i].slot,
		    this->devices[i].func, this->devices[i].vendor_id,
		    this->devices[i].device_id, this->devices[i].class_code,
		    this->devices[i].subclass,
		    this->class_name(devices[i].class_code,
				     this->devices[i].subclass));
	}
	LOG("devices=%d", this->num_devices);
#endif
}

void Pci::check_bus(std::uint8_t bus)
{
	for (std::uint8_t slot = 0; slot < 32; ++slot)
		this->check_device(bus, slot);
}

void Pci::check_device(std::uint8_t bus, std::uint8_t slot)
{
	std::uint16_t vendor = this->read_vendor(bus, slot, 0);
	if (vendor == 0xFFFF)
	{
#ifdef DEBUG
		LOG("vendor_not_detected");
#endif
		return;
	}
	this->check_function(bus, slot, 0);
	std::uint8_t header = this->read_header_type(bus, slot, 0);
	if (header & PCI_HEADER_TYPE_MULTI)
	{
		for (std::uint8_t func = 1; func < 8; ++func)
		{
			if (this->read_vendor(bus, slot, func) != 0xFFFF)
				this->check_function(bus, slot, func);
		}
	}
}

void Pci::check_function(std::uint8_t bus, std::uint8_t slot, std::uint8_t func)
{
	if (this->num_devices >= PCI_MAX_DEVICES)
		return;
	auto& dev = this->devices[this->num_devices];
	dev.bus = bus;
	dev.slot = slot;
	dev.func = func;
	dev.vendor_id = this->read_vendor(bus, slot, func);
	dev.device_id = this->read_device_id(bus, slot, func);
	dev.class_code = this->read_class(bus, slot, func);
	dev.subclass = this->read_subclass(bus, slot, func);
	dev.prog_if = this->read_prog_if(bus, slot, func);
	dev.header_type = this->read_header_type(bus, slot, func);
	num_devices++;

	if ((dev.header_type & ~PCI_HEADER_TYPE_MULTI) ==
	    PCI_HEADER_TYPE_BRIDGE)
	{
		std::uint8_t secondary_bus = static_cast<std::uint8_t>(
		    this->read_config(bus, slot, func, PCI_SECONDARY_BUS) >> 8);
		if (secondary_bus != bus)
			this->check_bus(secondary_bus);
	}
}

const char* Pci::class_name(std::uint8_t class_code, std::uint8_t subclass)
{
	switch (class_code)
	{
	case 0x00:
		if (subclass == 0x00)
			return "Unclassified (pre-2.0)";
		if (subclass == 0x01)
			return "VGA-compatible";
		return "Unclassified";
	case 0x01:
		if (subclass == 0x00)
			return "SCSI";
		if (subclass == 0x01)
			return "IDE";
		if (subclass == 0x02)
			return "Floppy";
		if (subclass == 0x03)
			return "IPI";
		if (subclass == 0x04)
			return "RAID";
		if (subclass == 0x05)
			return "ATA (single DMA)";
		if (subclass == 0x06)
			return "SATA";
		if (subclass == 0x07)
			return "Serial SCSI (SAS)";
		if (subclass == 0x08)
			return "NVM Express";
		if (subclass == 0x09)
			return "UFS";
		return "Mass storage";
	case 0x02:
		if (subclass == 0x00)
			return "Ethernet";
		if (subclass == 0x01)
			return "Token Ring";
		if (subclass == 0x02)
			return "FDDI";
		if (subclass == 0x03)
			return "ATM";
		return "Network";
	case 0x03:
		if (subclass == 0x00)
			return "VGA/8514";
		if (subclass == 0x01)
			return "XGA";
		if (subclass == 0x02)
			return "3D";
		return "Display";
	case 0x04:
		if (subclass == 0x00)
			return "Video";
		if (subclass == 0x01)
			return "Audio";
		if (subclass == 0x02)
			return "Telephony";
		return "Multimedia";
	case 0x05:
		if (subclass == 0x00)
			return "RAM";
		if (subclass == 0x01)
			return "Flash";
		return "Memory controller";
	case 0x06:
		if (subclass == 0x00)
			return "Host bridge";
		if (subclass == 0x01)
			return "ISA bridge";
		if (subclass == 0x02)
			return "EISA bridge";
		if (subclass == 0x03)
			return "MCA bridge";
		if (subclass == 0x04)
			return "PCI-to-PCI bridge";
		if (subclass == 0x05)
			return "PCMCIA bridge";
		if (subclass == 0x06)
			return "NuBus bridge";
		if (subclass == 0x07)
			return "CardBus bridge";
		if (subclass == 0x08)
			return "RACEway bridge";
		if (subclass == 0x09)
			return "PCI-to-PCI (semi-transparent)";
		if (subclass == 0x0A)
			return "InfiniBand";
		return "Bridge";
	case 0x07:
		if (subclass == 0x00)
			return "Serial (8250)";
		if (subclass == 0x01)
			return "Parallel (SPP)";
		if (subclass == 0x02)
			return "Multiport serial";
		if (subclass == 0x03)
			return "Modem";
		return "Communication";
	case 0x08:
		if (subclass == 0x00)
			return "PIC (generic)";
		if (subclass == 0x01)
			return "PIC (IO-APIC)";
		if (subclass == 0x02)
			return "PIC (IOX-APIC)";
		if (subclass == 0x03)
			return "Timer (8254)";
		if (subclass == 0x04)
			return "Timer (HPET)";
		return "System peripheral";
	case 0x09:
		return "Input device";
	case 0x0A:
		return "Docking station";
	case 0x0B:
		if (subclass == 0x00)
			return "FireWire (IEEE 1394)";
		if (subclass == 0x01)
			return "ACCESS bus";
		if (subclass == 0x02)
			return "SSA";
		return "Serial bus";
	case 0x0C:
		if (subclass == 0x00)
			return "USB (UHCI)";
		if (subclass == 0x01)
			return "USB (OHCI)";
		if (subclass == 0x02)
			return "USB (EHCI)";
		if (subclass == 0x03)
			return "USB (XHCI)";
		if (subclass == 0x04)
			return "SMBus";
		if (subclass == 0x05)
			return "CAN bus";
		return "Serial bus controller";
	case 0x0D:
		return "Wireless controller";
	case 0x0E:
		return "Intelligent I/O";
	case 0x0F:
		return "Satellite comm";
	case 0x10:
		return "Encryption";
	case 0x11:
		return "Data acquisition";
	case 0x12:
		return "Processing accelerator";
	default:
		return "Unknown";
	}
}

} // namespace pci
