#pragma once

#include <cstddef>
#include <cstdint>
#include <limine.h>

namespace gdt
{
struct entry;
struct tss;
}

namespace smp
{

static constexpr std::uint64_t MAX_CPUS = 64;
static constexpr std::uint64_t AP_STACK_SIZE = 16384;

struct cpu_info {
	cpu_info* self;
	std::uint64_t cpu_id;
	std::uint32_t lapic_id;
	bool online;
	bool bsp;
	bool startup_failed;
	std::uint64_t stack_base;
	void* current_thread;
	void* tss;
};

static_assert(offsetof(cpu_info, self) == 0,
              "cpu_info.self must be at offset 0 for gs:0 fetch");

extern cpu_info cpu_infos[MAX_CPUS];
extern std::uint64_t cpu_count;

extern "C" void ap_entry(struct limine_mp_info* info);

void wake_aps(struct limine_mp_response* mp);

void sse_enable(void);

static inline cpu_info* this_cpu()
{
	cpu_info* info;
	asm volatile("mov %%gs:0, %0" : "=r"(info));
	return info;
}

}