#include <ports/ports.hpp>

namespace ports
{

std::uint8_t inb(std::uint16_t port)
{
	std::uint8_t ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outb(std::uint16_t port, std::uint8_t val)
{
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

} // namespace ports
