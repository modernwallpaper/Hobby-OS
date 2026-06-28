#pragma once

#include <cstdint>

namespace ports
{

/**
 * Read a byte from an I/O port (x86 `inb`).
 *
 * Reads exactly 8 bits from the specified I/O port using the x86 `inb`
 * instruction.
 * @param port  I/O port address (0–65535).
 * @return      The 8-bit value read from the port.
 */
std::uint8_t inb(std::uint16_t port);

/**
 * Write a byte to an I/O port (x86 `outb`).
 *
 * Writes exactly 8 bits to the specified I/O port using the x86 `outb`
 * instruction.
 *
 * @param port  I/O port address (0–65535).
 * @param val   The 8-bit value to write.
 */
void outb(std::uint16_t port, std::uint8_t val);

std::uint32_t inl(std::uint16_t port);

void outl(std::uint16_t port, std::uint32_t value);

} // namespace ports
