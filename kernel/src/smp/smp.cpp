#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <gdt/gdt.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <smp/smp.hpp>

namespace smp
{

cpu_info cpu_infos[MAX_CPUS];
std::uint64_t cpu_count = 0;

static gdt::entry ap_gdts[MAX_CPUS][7];
static gdt::tss ap_tsss[MAX_CPUS] __attribute__((aligned(16)));

static constexpr std::uint32_t MSR_GS_BASE = 0xC0000101;

static void wrmsr(std::uint32_t msr, std::uint64_t value)
{
	std::uint32_t eax = value & 0xFFFFFFFF;
	std::uint32_t edx = (value >> 32) & 0xFFFFFFFF;
	__asm__ volatile("wrmsr" : : "c"(msr), "a"(eax), "d"(edx));
}

void sse_enable(void)
{
	std::uint64_t cr0;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 &= ~(1ULL << 2);
	cr0 |= (1ULL << 1);
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

	std::uint64_t cr4;
	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1ULL << 9);
	cr4 |= (1ULL << 10);
	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

static void ap_setup_gdt_tss(gdt::entry* gdt, gdt::tss* tss,
			     std::uint64_t stack_top)
{
	memory::memset(gdt, 0, 7 * sizeof(gdt::entry));

	gdt[1].access = 0x9A;
	gdt[1].flags = 0x0A;
	gdt[1].limit = 0x00;

	gdt[2].access = 0x92;
	gdt[2].flags = 0x0C;
	gdt[2].limit = 0x00;

	gdt[3].access = 0xFA;
	gdt[3].flags = 0x0A;
	gdt[3].limit = 0x00;

	gdt[4].access = 0xF2;
	gdt[4].flags = 0x0C;
	gdt[4].limit = 0x00;

	std::uint64_t tss_base = reinterpret_cast<std::uint64_t>(tss);
	std::uint32_t tss_limit = sizeof(gdt::tss) - 1;

	gdt[5].limit_low = tss_limit & 0xFFFF;
	gdt[5].base_low = tss_base & 0xFFFF;
	gdt[5].base_mid = (tss_base >> 16) & 0xFF;
	gdt[5].access = 0x89;
	gdt[5].limit = (tss_limit >> 16) & 0x0F;
	gdt[5].base_high = (tss_base >> 24) & 0xFF;

	auto* tss_high = reinterpret_cast<gdt::tss_entry*>(&gdt[6]);
	tss_high->base_high = (tss_base >> 32) & 0xFFFFFFFF;
	tss_high->reserved = 0;

	memory::memset(tss, 0, sizeof(gdt::tss));
	tss->io_map_base = sizeof(gdt::tss);
	tss->rsp0 = stack_top;

	gdt::ptr gdt_ptr;
	gdt_ptr.limit = 7 * sizeof(gdt::entry) - 1;
	gdt_ptr.base = reinterpret_cast<std::uint64_t>(gdt);

	gdt::gdt_flush(&gdt_ptr);
}

extern "C" void ap_entry(struct limine_mp_info* info)
{
	sse_enable();

	std::uint64_t cpu_id = info->extra_argument;
	cpu_info* my_cpu = &cpu_infos[cpu_id];

	register cpu_info* my_cpu_reg __asm__("r12") = my_cpu;

	wrmsr(MSR_GS_BASE, reinterpret_cast<std::uint64_t>(my_cpu));

	std::uint64_t ap_stack_top = my_cpu->stack_base + AP_STACK_SIZE;
	__asm__ volatile("mov %0, %%rsp" : : "r"(ap_stack_top));

	ap_setup_gdt_tss(ap_gdts[cpu_id], &ap_tsss[cpu_id], ap_stack_top);
	my_cpu_reg->tss = &ap_tsss[cpu_id];

	interrupts::apic::apic.init(acpi::acpi.lapic_address);
	interrupts::apic::apic.enable_x2apic();

#ifdef DEBUG
	LOG("ap=%u; apic_id=0x%x; online=true", cpu_id,
	    interrupts::apic::apic.get_id());
#endif

	my_cpu_reg->online = true;

	for (;;)
		__asm__ volatile("hlt");
}

void wake_aps(struct limine_mp_response* mp)
{
	if (!mp)
	{
		PANIC("mp_response=nullptr");
	}

	cpu_count = mp->cpu_count;

	for (std::uint64_t i = 0; i < cpu_count; i++)
	{
		auto* info = mp->cpus[i];
		cpu_infos[i].self = &cpu_infos[i];
		cpu_infos[i].cpu_id = i;
		cpu_infos[i].lapic_id = info->lapic_id;
		cpu_infos[i].bsp = (info->lapic_id == mp->bsp_lapic_id);
		cpu_infos[i].online = cpu_infos[i].bsp;
		cpu_infos[i].startup_failed = false;
		cpu_infos[i].stack_base = 0;
		cpu_infos[i].tss = nullptr;
	}

	wrmsr(MSR_GS_BASE, reinterpret_cast<std::uint64_t>(&cpu_infos[0]));

#ifdef DEBUG
	LOG("cpu_count=%u", cpu_count);
#endif

	// Batch-allocate all AP stacks first (before waking any AP)
	for (std::uint64_t i = 0; i < cpu_count; i++)
	{
		if (cpu_infos[i].bsp)
			continue;

#ifdef DEBUG
		LOG("alloc_stack_ap_%u", i);
#endif

		std::uint64_t stack_phys = memory::buddy.alloc_pages(2);
		if (!stack_phys)
			PANIC("failed to alloc stack for AP %u", i);

		cpu_infos[i].stack_base = reinterpret_cast<std::uint64_t>(
		    memory::phys_to_virt(stack_phys));
	}

	// Now wake APs one by one
	for (std::uint64_t i = 0; i < cpu_count; i++)
	{
		if (cpu_infos[i].bsp)
			continue;

		auto* info = mp->cpus[i];

#ifdef DEBUG
		LOG("waking_ap_%u", i);
#endif

		info->extra_argument = i;

		__atomic_store_n(&info->goto_address,
				 (limine_goto_address)ap_entry,
				 __ATOMIC_SEQ_CST);

#ifdef DEBUG
		LOG("woken_ap_%u", i);
#endif

		std::uint64_t spins = 0;
		while (!cpu_infos[i].online && spins < 1000000)
		{
			__asm__ volatile("pause" : : : "memory");
			spins++;
		}

		if (!cpu_infos[i].online)
		{
			cpu_infos[i].startup_failed = true;
#ifdef DEBUG
			LOG("AP_%u_startup_timed_out", i);
#endif
		}
	}

#ifdef DEBUG
	std::uint64_t online = 0;
	for (std::uint64_t i = 0; i < cpu_count; i++)
		if (cpu_infos[i].online && !cpu_infos[i].startup_failed)
			online++;
	LOG("online=%u; count=%u", online, cpu_count);
#endif
}

} // namespace smp
