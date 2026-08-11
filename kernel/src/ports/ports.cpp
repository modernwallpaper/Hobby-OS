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

std::uint16_t inw(std::uint16_t port)
{
	std::uint16_t ret;
	__asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outw(std::uint16_t port, std::uint16_t val)
{
	__asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

std::uint32_t inl(std::uint16_t port)
{
	std::uint32_t ret;
	__asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outl(std::uint16_t port, std::uint32_t value)
{
	__asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

}
