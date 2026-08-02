#pragma once

#include <cstdint>

namespace interrupts
{

namespace idt
{

struct entry {
	std::uint16_t offset_low; // offset bits 0..15
	std::uint16_t selector;	  // a code segment selector in GDT or LDT
	std::uint8_t ist; // bits 0..2 holds Interrupt Stack Table offset, rest
			  // of bits zero.
	std::uint8_t type_attributes; // gate type, dpl, and p fields
	std::uint16_t offset_mid;     // offset bits 16..31
	std::uint32_t offset_high;    // offset bits 32..63
	std::uint32_t zero;
} __attribute__((packed));

struct ptr {
	std::uint16_t limit;
	std::uint64_t base;
} __attribute__((packed));

struct frame {
	std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	std::uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

	std::uint64_t vector;
	std::uint64_t error;

	std::uint64_t rip;
	std::uint64_t cs;
	std::uint64_t rflags;
	std::uint64_t rsp;
	std::uint64_t ss;
} __attribute__((packed));

class IDT {
private:
	entry idt[256];
	ptr idt_ptr;

public:
	void init(void);
	void load(void);
	void set_gate(int n, void* handler, std::uint8_t ist = 0);
	void set_ist(int n, std::uint8_t ist);
};

extern IDT idt;
extern "C" frame* isr_handler(frame* frame);
extern "C" void irq_handler(frame* frame);

void register_irq_handler(int vector, void (*handler)(frame*));

} // namespace idt

} // namespace interrupts
