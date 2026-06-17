#pragma once
#include <logging/logger.hpp>
#include <ports/ports.hpp>

namespace interrupts
{

namespace pic
{

#define PIC1 0x20 /* IO base address for master PIC */
#define PIC2 0xA0 /* IO base address for slave PIC */
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)

static void disable_pic(void)
{
	ports::outb(PIC1_DATA, 0xFF);
	ports::outb(PIC2_DATA, 0xFF);
#ifdef DEBUG
	LOG("disabled");
#endif
}

} // namespace pic

} // namespace interrupts
