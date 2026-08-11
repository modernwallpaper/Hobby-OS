#include <apic/apic.hpp>
#include <idt/idt.hpp>
#include <logging/logger.hpp>
#include <memory/slub.hpp>
#include <panic/panic.hpp>
#include <sched/sched.hpp>

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
extern "C" void yield_stub(void);

extern "C" void ext_irq0(void);
extern "C" void ext_irq1(void);
extern "C" void ext_irq2(void);
extern "C" void ext_irq3(void);
extern "C" void ext_irq4(void);
extern "C" void ext_irq5(void);
extern "C" void ext_irq6(void);
extern "C" void ext_irq7(void);
extern "C" void ext_irq8(void);
extern "C" void ext_irq9(void);
extern "C" void ext_irq10(void);
extern "C" void ext_irq11(void);
extern "C" void ext_irq12(void);
extern "C" void ext_irq13(void);
extern "C" void ext_irq14(void);
extern "C" void ext_irq15(void);
extern "C" void ext_irq16(void);
extern "C" void ext_irq17(void);
extern "C" void ext_irq18(void);
extern "C" void ext_irq19(void);
extern "C" void ext_irq20(void);
extern "C" void ext_irq21(void);
extern "C" void ext_irq22(void);
extern "C" void ext_irq23(void);
extern "C" void ext_irq24(void);
extern "C" void ext_irq25(void);
extern "C" void ext_irq26(void);
extern "C" void ext_irq27(void);
extern "C" void ext_irq28(void);
extern "C" void ext_irq29(void);
extern "C" void ext_irq30(void);
extern "C" void ext_irq31(void);

static void (*ext_irq_table[32])(void) = {
    ext_irq0,  ext_irq1,  ext_irq2,  ext_irq3,	 ext_irq4,  ext_irq5,
    ext_irq6,  ext_irq7,  ext_irq8,  ext_irq9,	 ext_irq10, ext_irq11,
    ext_irq12, ext_irq13, ext_irq14, ext_irq15, ext_irq16, ext_irq17,
    ext_irq18, ext_irq19, ext_irq20, ext_irq21, ext_irq22, ext_irq23,
    ext_irq24, ext_irq25, ext_irq26, ext_irq27, ext_irq28, ext_irq29,
    ext_irq30, ext_irq31};

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

static interrupts::idt::irq_handler_entry* irq_handler_table[256] = {};

void register_irq_handler(int vector, void (*handler)(frame*))
{
	if (vector < 0 || vector >= 256 || handler == nullptr)
		return;
	auto* entry = static_cast<interrupts::idt::irq_handler_entry*>(
	    memory::slub.kmalloc(
		sizeof(interrupts::idt::irq_handler_entry)));
	entry->handler = handler;
	entry->next = nullptr;

	// keep every handler registered on this vector: a shared INTx line
	// delivers the same vector to all devices on it, and each driver
	// checks its own pending status and services only itself
	if (irq_handler_table[vector] == nullptr)
	{
		irq_handler_table[vector] = entry;
		return;
	}
	interrupts::idt::irq_handler_entry* cur = irq_handler_table[vector];
	while (cur->next != nullptr)
		cur = cur->next;
	cur->next = entry;
}

// main interrupt handler: dispatch exceptions and IRQs
extern "C" frame* isr_handler(frame* frame)
{
	// #ifdef DEBUG
	// 	LOG("vector=%x", frame->vector);
	// #endif
	if (frame->vector == LAPIC_TIMER_VECTOR)
	{
		apic::apic.eoi();
		apic::apic.timer_oneshot_periodic_tick();
		return sched::scheduler.tick(frame);
	}
	else if (frame->vector == YIELD_VECTOR)
	{
		return sched::scheduler.yield(frame);
	}
	else if (frame->vector >= 32 && frame->vector < 256 &&
		 (frame->vector <= DEVICE_IRQ_BASE + 15 ||
		  irq_handler_table[frame->vector] != nullptr))
	{
		irq_handler(frame);
	}
	else if (frame->vector < 32)
	{
		// CPU exceptions are (almost) never recoverable. Faults like #PF/#GP
		// caused by a wild pointer in a driver will otherwise re-trigger
		// forever, making the machine appear hung. Panic instead.
		PANIC("unhandled_exception; rip=%p; rsp=%p; vector=%llu; error=%p; "
		      "cs=%llx; rflags=%llx; rax=%p; rbx=%p; rcx=%p; rdx=%p; "
		      "rsi=%p; rdi=%p",
		      frame->rip, frame->rsp, frame->vector, frame->error,
		      frame->cs, frame->rflags, frame->rax, frame->rbx,
		      frame->rcx, frame->rdx, frame->rsi, frame->rdi);
	}
	else
	{
#ifdef DEBUG
		LOG("unhandled_interrupt; rip=%p; vector=%llu",
		    frame->rip, frame->vector);
#endif
	}
	return frame;
}

// IRQ handler: run every handler registered for this vector (a vector may be
// shared by several devices), then send EOI to the APIC
extern "C" void irq_handler(frame* frame)
{
	for (auto* cur = irq_handler_table[frame->vector]; cur != nullptr;
	     cur = cur->next)
		cur->handler(frame);
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
		if (i == LAPIC_TIMER_VECTOR)
			this->set_gate(
			    i, reinterpret_cast<void*>(lapic_timer_stub));
		else if (i == YIELD_VECTOR)
			this->set_gate(i, reinterpret_cast<void*>(yield_stub));
		else
		{
			this->set_gate(i,
				       reinterpret_cast<void*>(isr_unhandled));
		}
	}

	// device IRQ gates must be wired after the 48..255 sweep above so the
	// generic isr_unhandled gate does not overwrite them
	for (int i = 0; i < 32; ++i)
		this->set_gate(DEVICE_IRQ_BASE + i,
			       reinterpret_cast<void*>(ext_irq_table[i]));

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

}
}
