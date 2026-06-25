#include <pci/pci.hpp>

namespace pci
{

Pci pci;

static inline std::uint32_t inl(std::uint16_t port)
{
	std::uint32_t ret;
	__asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline void outl(std::uint16_t port, std::uint32_t value)
{
	__asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

std::uint32_t Pci::read_config(std::uint8_t bus, std::uint8_t slot,
			       std::uint8_t func, std::uint8_t offset)
{
	std::uint32_t address = static_cast<std::uint32_t>(
	    (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) |
	    0x80000000);
	outl(PCI_CONFIG_ADDR, address);
	return inl(PCI_CONFIG_DATA);
}

} // namespace pci
