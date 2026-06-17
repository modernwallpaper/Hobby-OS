#include <apic/apic.hpp>
#include <idt/idt.hpp>
#include <logging/logger.hpp>

namespace interrupts
{

namespace idt
{

IDT idt;

extern "C" void isr0(void);
extern "C" void isr1(void);
extern "C" void isr2(void);
extern "C" void isr3(void);
extern "C" void isr4(void);
extern "C" void isr5(void);
extern "C" void isr6(void);
extern "C" void isr7(void);
extern "C" void isr8(void);
extern "C" void isr9(void);
extern "C" void isr10(void);
extern "C" void isr11(void);
extern "C" void isr12(void);
extern "C" void isr13(void);
extern "C" void isr14(void);
extern "C" void isr15(void);
extern "C" void isr16(void);
extern "C" void isr17(void);
extern "C" void isr18(void);
extern "C" void isr19(void);
extern "C" void isr20(void);
extern "C" void isr21(void);
extern "C" void isr22(void);
extern "C" void isr23(void);
extern "C" void isr24(void);
extern "C" void isr25(void);
extern "C" void isr26(void);
extern "C" void isr27(void);
extern "C" void isr28(void);
extern "C" void isr29(void);
extern "C" void isr30(void);
extern "C" void isr31(void);
extern "C" void isr_unhandled(void);
extern "C" void lapic_timer_stub(void);

static void (*isr_table[32])(void) = {
    isr0,  isr1,  isr2,	 isr3,	isr4,  isr5,  isr6,  isr7,  isr8,  isr9,  isr10,
    isr11, isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19, isr20, isr21,
    isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31};

extern "C" void irq0(void);
extern "C" void irq1(void);
extern "C" void irq2(void);
extern "C" void irq3(void);
extern "C" void irq4(void);
extern "C" void irq5(void);
extern "C" void irq6(void);
extern "C" void irq7(void);
extern "C" void irq8(void);
extern "C" void irq9(void);
extern "C" void irq10(void);
extern "C" void irq11(void);
extern "C" void irq12(void);
extern "C" void irq13(void);
extern "C" void irq14(void);
extern "C" void irq15(void);

static void (*irq_table[16])(void) = {irq0,  irq1,  irq2,  irq3, irq4,	irq5,
				      irq6,  irq7,  irq8,  irq9, irq10, irq11,
				      irq12, irq13, irq14, irq15};

void lapic_timer_handler(frame* frame)
{
	apic::apic.eoi();
}

// main interrupt handler: dispatch exceptions and IRQs
extern "C" frame* isr_handler(frame* frame)
{
	// #ifdef DEBUG
	// 	LOG("vector=%x", frame->vector);
	// #endif
	if (frame->vector == 0)
	{
		frame->rip += 2;
	}
	else if (frame->vector >= 32 && frame->vector <= 47)
	{
		irq_handler(frame);
	}
	else if (frame->vector == 48)
	{
		lapic_timer_handler(frame);
	}
	else
	{
#ifdef DEBUG
		LOG("unhandled_exception; rip=%x; rsp=%x; vector=%d; error=%x",
		    frame->rip, frame->rsp, frame->vector, frame->error);
#endif
	}
	return frame;
}

// IRQ handler: send EOI to the APIC
extern "C" void irq_handler(frame* frame)
{
	// #ifdef DEBUG
	// 	std::uint8_t irq = frame->vector - 32;
	// 	LOG("irq=%x", irq);
	// #endif
	apic::apic.eoi();
}

// populate the IDT: cpu exception ISRs, IRQs, and IST entries for critical
// vectors
void IDT::init(void)
{
	for (int i = 0; i < 32; ++i)
		this->set_gate(i, reinterpret_cast<void*>(isr_table[i]));

	for (int i = 0; i < 16; ++i)
		this->set_gate(32 + i, reinterpret_cast<void*>(irq_table[i]));

	for (int i = 48; i < 256; ++i)
	{
		if (i == 48)
			this->set_gate(
			    i, reinterpret_cast<void*>(lapic_timer_stub));
		else
		{
			this->set_gate(i,
				       reinterpret_cast<void*>(isr_unhandled));
		}
	}

	this->set_ist(1, 1);
	this->set_ist(2, 2);
	this->set_ist(8, 4);
	this->set_ist(18, 3);

	this->load();
#ifdef DEBUG
	LOG("initialized");
#endif
}

// configure one IDT entry with handler address, code selector, and optional IST
void IDT::set_gate(int n, void* handler, std::uint8_t ist)
{
	std::uint64_t address = reinterpret_cast<std::uint64_t>(handler);
#ifdef DEBUG
	LOG("n=%d; handler_address=%p; ist=%u", n, address, ist);
#endif
	this->idt[n].offset_low = address & 0xFFFF;
	this->idt[n].selector = 0x08;
	this->idt[n].ist = ist & 0x07;
	this->idt[n].type_attributes = 0x8E;
	this->idt[n].offset_mid = (address >> 16) & 0xFFFF;
	this->idt[n].offset_high = (address >> 32) & 0xFFFFFFFF;
	this->idt[n].zero = 0;
}

void IDT::set_ist(int n, std::uint8_t ist)
{
	this->idt[n].ist = ist & 0x07;
}

// load the IDT via the lidt instruction
void IDT::load(void)
{
	this->idt_ptr.limit = sizeof(this->idt) - 1;
	this->idt_ptr.base = reinterpret_cast<std::uint64_t>(&this->idt);
	asm volatile("lidt %0" : : "m"(this->idt_ptr));
}

} // namespace idt
} // namespace interrupts
