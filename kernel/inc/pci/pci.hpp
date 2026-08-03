#pragma once

#include <cstdint>

namespace pci
{
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_REVISION_ID 0x08
#define PCI_PROG_IF 0x09
#define PCI_SUBCLASS 0x0A
#define PCI_CLASS 0x0B
#define PCI_HEADER_TYPE 0x0E
#define PCI_BIST 0x0F
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24
#define PCI_SUBSYS_VENDOR 0x2C
#define PCI_SUBSYS_ID 0x2E
#define PCI_CAP_PTR 0x34
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN 0x3D
#define PCI_SECONDARY_BUS 0x19

#define PCI_HEADER_TYPE_NORMAL 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01
#define PCI_HEADER_TYPE_MULTI 0x80

#define PCI_CLASS_UNCLASSIFIED 0x00
#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_DISPLAY 0x03
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_CLASS_MEMORY 0x05
#define PCI_CLASS_BRIDGE 0x06
#define PCI_CLASS_SERIAL 0x0C

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 256

#define PCI_BAR_TYPE_IO 0x01
#define PCI_BAR_TYPE_MEM 0x00
#define PCI_BAR_64_BIT 0x04
#define PCI_BAR_PREFETCH 0x08

// PCI capability IDs
#define PCI_CAP_POWER_MGMT 0x01
#define PCI_CAP_MSI 0x05
#define PCI_CAP_PCIE 0x10
#define PCI_CAP_MSIX 0x11

// MSI message control register bits
#define PCI_MSI_CTRL_ENABLE (1 << 0)
#define PCI_MSI_CTRL_MME_MASK (0x7 << 4)
#define PCI_MSI_CTRL_64BIT (1 << 7)
#define PCI_MSI_CTRL_PVMC (1 << 8)

struct device {
	std::uint8_t bus;
	std::uint8_t slot;
	std::uint8_t func;
	std::uint16_t vendor_id;
	std::uint16_t device_id;
	std::uint8_t class_code;
	std::uint8_t subclass;
	std::uint8_t prog_if;
	std::uint8_t header_type;
};

// decoded contents of one BAR register
struct bar_info {
	std::uint64_t address;
	std::uint64_t size;
	bool is_io;
	bool is_64;
	bool prefetchable;
};

class Pci {
	int num_devices;
	device devices[PCI_MAX_DEVICES];

	void check_bus(std::uint8_t bus);
	void check_device(std::uint8_t bus, std::uint8_t slot);
	void check_function(std::uint8_t bus, std::uint8_t slot,
			    std::uint8_t func);

public:
	void init(void);
	void enumerate(void);
	std::uint32_t read_config(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func, std::uint8_t offset);
	void write_config(std::uint8_t bus, std::uint8_t slot,
			  std::uint8_t func, std::uint8_t offset,
			  std::uint32_t value);

	std::uint16_t read_config16(std::uint8_t bus, std::uint8_t slot,
				    std::uint8_t func, std::uint8_t offset);
	void write_config16(std::uint8_t bus, std::uint8_t slot,
			    std::uint8_t func, std::uint8_t offset,
			    std::uint16_t value);

	std::uint16_t read_vendor(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func);
	std::uint16_t read_device_id(std::uint8_t bus, std::uint8_t slot,
				     std::uint8_t func);
	std::uint8_t read_class(std::uint8_t bus, std::uint8_t slot,
				std::uint8_t func);
	std::uint8_t read_subclass(std::uint8_t bus, std::uint8_t slot,
				   std::uint8_t func);
	std::uint8_t read_prog_if(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func);
	std::uint8_t read_header_type(std::uint8_t bus, std::uint8_t slot,
				      std::uint8_t func);
	std::uint32_t read_bar(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func, int bar);
	bar_info read_bar_info(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func, int bar);

	std::uint8_t find_capability(std::uint8_t bus, std::uint8_t slot,
				     std::uint8_t func, std::uint8_t cap_id);

	int device_count() const
	{
		return num_devices;
	}
	const device* get_device(int index) const;

	static const char* class_name(std::uint8_t class_code,
				      std::uint8_t subclass);
};

extern Pci pci;

} // namespace pci
