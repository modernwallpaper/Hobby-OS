#pragma once

#include <cstdint>

namespace ports
{

std::uint8_t inb(std::uint16_t port);

void outb(std::uint16_t port, std::uint8_t val);

std::uint16_t inw(std::uint16_t port);

void outw(std::uint16_t port, std::uint16_t val);

std::uint32_t inl(std::uint16_t port);

void outl(std::uint16_t port, std::uint32_t value);

}
