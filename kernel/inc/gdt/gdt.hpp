#pragma once

#include <cstdint>

namespace gdt
{

struct entry {
	std::uint16_t limit_low; // 0-1:  Limit[15:0]
	std::uint16_t base_low;	 // 2-3:  Base[15:0]
	std::uint8_t base_mid;	 // 4:    Base[23:16]
	std::uint8_t access;	 // 5:    Access Byte
	std::uint8_t limit : 4;	 // 6:    Limit[19:16] (low nibble)
	std::uint8_t flags : 4;	 // 6:    Flags (high nibble)
	std::uint8_t base_high;	 // 7:    Base[31:24]
} __attribute__((packed));

struct ptr {
	std::uint16_t limit; // 0-1: Limit
	std::uint64_t base;  // 2-9: Base
} __attribute__((packed));

struct tss_entry {
	std::uint32_t base_high;
	std::uint32_t reserved;
} __attribute__((packed));

struct tss {
	std::uint32_t reserved0;
	std::uint64_t rsp0;
	std::uint64_t rsp1;
	std::uint64_t rsp2;
	std::uint64_t reserved1;
	std::uint64_t ist1;
	std::uint64_t ist2;
	std::uint64_t ist3;
	std::uint64_t ist4;
	std::uint64_t ist5;
	std::uint64_t ist6;
	std::uint64_t ist7;
	std::uint64_t reseerved2;
	std::uint16_t reserved3;
	std::uint16_t io_map_base;
} __attribute__((packed));

extern "C" void gdt_flush(gdt::ptr* gdt_ptr);

class GDT {
private:
	static constexpr int ENTRIES = 7;
	entry gdt_entries[ENTRIES];
	ptr gdt_ptr;
	tss_entry* tss_high_entry;
	struct tss tss __attribute__((aligned(16)));

	std::uint64_t KERNEL_STACK_SIZE;
	std::uint8_t* kernel_stack;

public:
	void init(std::uint64_t kernel_stack_size, std::uint8_t* kernel_stack,
		  std::uint64_t ist_stack_size, std::uint8_t* ist1,
		  std::uint8_t* ist2, std::uint8_t* ist3, std::uint8_t* ist4);
	void set_gate(std::uint32_t num, std::uint32_t base,
		      std::uint32_t limit, std::uint8_t access,
		      std::uint8_t granularity);
	void set_tss_gate(std::uint32_t num, std::uint64_t base,
			  std::uint32_t limit);
	struct tss* get_tss()
	{
		return &this->tss;
	}
	struct ptr* get_gdt_ptr()
	{
		return &this->gdt_ptr;
	}
};

extern GDT gdt;

}
