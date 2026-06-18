#include <smp/smp.hpp>
#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <gdt/gdt.hpp>
#include <hpet/hpet.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <memory/paging.hpp>

namespace smp
{

extern "C" {
extern char _binary_obj_x86_64_smp_trampoline_bin_start[];
extern char _binary_obj_x86_64_smp_trampoline_bin_end[];
}

// Offsets within the trampoline page — must match smp_trampoline.asm
namespace info_offsets
{
static constexpr std::uintptr_t GDT_LIMIT = 0x02;
static constexpr std::uintptr_t GDT_BASE = 0x04;
static constexpr std::uintptr_t CR3 = 0x0C;
static constexpr std::uintptr_t STACK_TOP = 0x14;
static constexpr std::uintptr_t ENTRY_FN = 0x1C;
static constexpr std::uintptr_t CPU_ID = 0x24;
static constexpr std::uintptr_t GDTR = 0x28;
static constexpr std::uintptr_t GDT_ENTRIES = 0x2E;
} // namespace info_offsets

// Read CR3
static std::uint64_t read_cr3(void)
{
	std::uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	return cr3;
}

// Busy-wait for `us` microseconds using the HPET counter
static void udelay(std::uint64_t us)
{
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	if (freq == 0)
		return;
	std::uint64_t ticks = freq * us / 1000000;
	std::uint64_t start = timers::hpet::hpet.read_counter();
	while (timers::hpet::hpet.read_counter() - start < ticks)
		__asm__ volatile("pause");
}

// C entry point called by each AP after the trampoline brings it to long mode
extern "C" void ap_entry(std::uint32_t cpu_id)
{
	interrupts::apic::apic.init(acpi::acpi.lapic_address);
	interrupts::apic::apic.enable_x2apic();

	LOG("AP_%u_online; apic_id=0x%x", cpu_id,
	    interrupts::apic::apic.get_id());

	for (;;)
		asm volatile("hlt");
}

void wake_aps(void)
{
	// ---- allocate pages for AP page tables ----
	std::uint64_t pml4_phys = memory::buddy.alloc_pages(0);
	std::uint64_t pdpt_phys = memory::buddy.alloc_pages(0);
	std::uint64_t pd_phys = memory::buddy.alloc_pages(0);

	if (!pml4_phys || !pdpt_phys || !pd_phys)
		PANIC("smp: failed to allocate page table pages");

	auto* pml4 = static_cast<std::uint64_t*>(
	    memory::phys_to_virt(pml4_phys));
	auto* pdpt = static_cast<std::uint64_t*>(
	    memory::phys_to_virt(pdpt_phys));
	auto* pd = static_cast<std::uint64_t*>(
	    memory::phys_to_virt(pd_phys));

	// ---- set up AP page tables ----
	memory::memset(pml4, 0, 0x1000);
	memory::memset(pdpt, 0, 0x1000);
	memory::memset(pd, 0, 0x1000);

	// Copy kernel higher-half entries (256–511) from BSP's PML4
	std::uint64_t bsp_pml4_phys = read_cr3();
	auto* bsp_pml4 = static_cast<std::uint64_t*>(
	    memory::phys_to_virt(bsp_pml4_phys));
	for (int i = 256; i < 512; i++)
		pml4[i] = bsp_pml4[i];

	// Identity-map first 2 MB via PML4[0] → PDPT[0] → PD[0] (2 MB huge page)
	pml4[0] = pdpt_phys | 0x03;   // present | writable
	pdpt[0] = pd_phys | 0x03;     // present | writable
	pd[0] = 0x00 | 0x83;          // present | writable | huge (PS=1), base = 0

	// ---- copy trampoline binary to the low page ----
	std::uintptr_t tramp_size =
	_binary_obj_x86_64_smp_trampoline_bin_end -
	    _binary_obj_x86_64_smp_trampoline_bin_start;

	if (tramp_size > 0x1000)
		PANIC("smp: trampoline too big (%u bytes)", tramp_size);

	auto* tramp = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(TRAMPOLINE_PAGE));
	memory::memcpy(tramp, _binary_obj_x86_64_smp_trampoline_bin_start, tramp_size);

	// ---- write GDT entries into the trampoline page (not defined in .asm) ----
	// Values use the same access-byte conventions as gdt.cpp.
	// Null descriptor
	std::uint64_t gdt_null = 0;
	memory::memcpy(tramp + info_offsets::GDT_ENTRIES,
	               &gdt_null, 8);
	// Code32:   access 0x9A (present, ring0, code, exec/read),
	//           gran 0xCF (G=1, D=1 → 32-bit, limit 0xFFFFF)
	std::uint64_t gdt_code32 = 0x00CF9A000000FFFF;
	memory::memcpy(tramp + info_offsets::GDT_ENTRIES + 8,
	               &gdt_code32, 8);
	// Data32:   access 0x92 (present, ring0, data, read/write),
	//           gran 0xCF (G=1, D=1)
	std::uint64_t gdt_data32 = 0x00CF92000000FFFF;
	memory::memcpy(tramp + info_offsets::GDT_ENTRIES + 16,
	               &gdt_data32, 8);
	// Code64:   access 0x9A (same as kernel code64),
	//           gran 0xAF (G=1, L=1 → 64-bit, limit 0xFFFFF)
	std::uint64_t gdt_code64 = 0x00AF9A000000FFFF;
	memory::memcpy(tramp + info_offsets::GDT_ENTRIES + 24,
	               &gdt_code64, 8);

	// Set up GDTR pointing to the entries above
	std::uint16_t gdt_tramp_limit = 4 * 8 - 1; // 31
	std::uint32_t gdt_tramp_base =
	    TRAMPOLINE_PAGE + info_offsets::GDT_ENTRIES;
	memory::memcpy(tramp + info_offsets::GDTR,
	               &gdt_tramp_limit, 2);
	memory::memcpy(tramp + info_offsets::GDTR + 2,
	               &gdt_tramp_base, 4);

	// ---- find BSP index ----
	int bsp_idx = -1;
	for (int i = 0; i < acpi::acpi.cpu_count; i++)
	{
		if (acpi::acpi.cpus[i].bsp)
		{
			bsp_idx = i;
			break;
		}
	}
	if (bsp_idx < 0)
		bsp_idx = 0;

	// ---- wake each AP ----
	for (int i = 0; i < acpi::acpi.cpu_count; i++)
	{
		if (i == bsp_idx)
			continue;

		std::uint8_t apic_id = acpi::acpi.cpus[i].apic_id;

		// Allocate stack (4 pages = 16 KiB)
		std::uint64_t stack_phys = memory::buddy.alloc_pages(2);
		if (!stack_phys)
			PANIC("smp: failed to alloc stack for AP %d", i);

		std::uint64_t stack_top =
		    (stack_phys + 0x4000) + memory::hhdm_offset;

		// ---- fill info structure in the trampoline page ----
		auto gdt_limit = static_cast<std::uint16_t>(
		    gdt::gdt.get_gdt_ptr()->limit);
		auto gdt_base = gdt::gdt.get_gdt_ptr()->base;

		memory::memcpy(tramp + info_offsets::GDT_LIMIT,
			       &gdt_limit, 2);
		memory::memcpy(tramp + info_offsets::GDT_BASE,
			       &gdt_base, 8);
		memory::memcpy(tramp + info_offsets::CR3,
			       &pml4_phys, 8);
		memory::memcpy(tramp + info_offsets::STACK_TOP,
			       &stack_top, 8);

		auto entry_fn =
		    reinterpret_cast<std::uint64_t>(ap_entry);
		memory::memcpy(tramp + info_offsets::ENTRY_FN,
			       &entry_fn, 8);

		auto cpu_id = static_cast<std::uint32_t>(i);
		memory::memcpy(tramp + info_offsets::CPU_ID,
			       &cpu_id, 4);

#ifdef DEBUG
		LOG("waking_ap_%d: apic_id=0x%x; stack_top=%p",
		    i, apic_id, stack_top);
#endif

		// ---- send INIT IPI ----
		interrupts::apic::apic.send_ipi(
		    apic_id, 0, 5); // ICR delivery mode: INIT
		udelay(10000);

		// ---- send first SIPI ----
		std::uint8_t vector =
		    static_cast<std::uint8_t>(TRAMPOLINE_PAGE >> 12);
		interrupts::apic::apic.send_startup_ipi(apic_id, vector);
		udelay(200);

		// ---- send second SIPI (recommended for reliability) ----
		interrupts::apic::apic.send_startup_ipi(apic_id, vector);
		udelay(200);
	}

#ifdef DEBUG
	LOG("smp: all APs woken");
#endif
}

} // namespace smp
