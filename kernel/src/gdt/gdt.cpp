#include <gdt/gdt.hpp>
#include <logging/logger.hpp>
#include <memory/memory.hpp>

namespace gdt
{

static_assert(sizeof(entry) == 8, "GDT entry must be 8 bytes");
static_assert(sizeof(tss) == 104, "TSS must be 104 bytes");

GDT gdt;

void GDT::init(std::uint64_t kernel_stack_size, std::uint8_t* kernel_stack,
	       std::uint64_t ist_stack_size, std::uint8_t* ist1,
	       std::uint8_t* ist2, std::uint8_t* ist3,
	       std::uint8_t* ist4)
{
	this->KERNEL_STACK_SIZE = kernel_stack_size;
	this->kernel_stack = kernel_stack;

	memory::memset(&this->gdt_entries, 0, sizeof(this->gdt_entries));

	this->set_gate(0, 0, 0, 0, 0);	     // Null descriptor
	this->set_gate(1, 0, 0, 0x9A, 0xA0); // Kernel Code:  L=1, DPL=0
	this->set_gate(2, 0, 0, 0x92, 0xC0); // Kernel Data:  W=1, DPL=0
	this->set_gate(3, 0, 0, 0xFA, 0xA0); // User Code:    L=1, DPL=3
	this->set_gate(4, 0, 0, 0xF2, 0xC0); // User Data:    W=1, DPL=3

	memory::memset(&this->tss, 0, sizeof(this->tss));

	std::uint64_t rsp0 =
	    reinterpret_cast<std::uint64_t>(this->kernel_stack) +
	    this->KERNEL_STACK_SIZE;
	this->tss.rsp0 = rsp0;
	this->tss.ist1 = reinterpret_cast<std::uint64_t>(ist1) + ist_stack_size;
	this->tss.ist2 = reinterpret_cast<std::uint64_t>(ist2) + ist_stack_size;
	this->tss.ist3 = reinterpret_cast<std::uint64_t>(ist3) + ist_stack_size;
	this->tss.ist4 = reinterpret_cast<std::uint64_t>(ist4) + ist_stack_size;
	this->tss.io_map_base = sizeof(gdt::tss);

	this->set_tss_gate(5, reinterpret_cast<std::uint64_t>(&this->tss),
			   sizeof(gdt::tss) - 1);

	this->gdt_ptr.base =
	    reinterpret_cast<std::uint64_t>(&this->gdt_entries[0]);
	this->gdt_ptr.limit = sizeof(this->gdt_entries) - 1;

	gdt_flush(&this->gdt_ptr);
#ifdef DEBUG
	LOG("limit=%d", this->gdt_ptr.limit);
#endif
}

void GDT::set_gate(std::uint32_t num, std::uint32_t base, std::uint32_t limit,
		   std::uint8_t access, std::uint8_t granularity)
{
#ifdef DEBUG
	LOG("num=%x; base=%x; limit=%x; access=%x; granularity=%x", num, base,
	    limit, access, granularity);
#endif
	this->gdt_entries[num].limit_low = limit & 0xFFFF;
	this->gdt_entries[num].base_low = base & 0xFFFF;
	this->gdt_entries[num].base_mid = (base >> 16) & 0xFF;
	this->gdt_entries[num].access = access;
	this->gdt_entries[num].limit = granularity & 0x0F;
	this->gdt_entries[num].flags = granularity >> 4;
	this->gdt_entries[num].base_high = (base >> 24) & 0xFF;
}

void GDT::set_tss_gate(std::uint32_t num, std::uint64_t base,
		       std::uint32_t limit)
{
#ifdef DEBUG
	LOG("num=%x; base=%p; limit=%x", num, base, limit);
#endif
	this->set_gate(num, base & 0xFFFFFFFF, limit, 0x89, 0x00);
	this->tss_high_entry =
	    reinterpret_cast<gdt::tss_entry*>(&this->gdt_entries[num + 1]);
	this->tss_high_entry->base_high = (base >> 32) & 0xFFFFFFFF;
	this->tss_high_entry->reserved = 0;
}

} // namespace gdt
