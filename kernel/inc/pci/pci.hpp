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

class Pci {
private:
	std::uint32_t read_config(std::uint8_t bus, std::uint8_t slot,
				  std::uint8_t func, std::uint8_t offset);

public:
};

extern Pci pci;

} // namespace pci
