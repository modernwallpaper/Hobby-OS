#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <apic/ioapic.hpp>
#include <cstddef>
#include <cstdint>
#include <drivers/storage/ahci.hpp>
#include <gdt/gdt.hpp>
#include <hpet/hpet.hpp>
#include <idt/idt.hpp>
#include <limine.h>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/slub.hpp>
#include <panic/panic.hpp>
#include <pci/pci.hpp>
#include <pic/pic.hpp>
#include <ports/ports.hpp>
#include <sched/sched.hpp>
#include <smp/smp.hpp>
#include <tests/tests.hpp>
#include <tsc/tsc.hpp>

namespace
{

__attribute__((used, section(".limine_requests"))) volatile std::uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(6);

} // namespace

namespace
{

__attribute__((
    used,
    section(
	".limine_requests"))) volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((
    used,
    section(".limine_requests"))) volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((
    used,
    section(".limine_requests"))) volatile limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((
    used,
    section(".limine_requests"))) volatile limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID, .revision = 0, .response = nullptr, .flags = 0};

__attribute__((
    used,
    section(".limine_requests"))) volatile limine_executable_address_request
    exec_addr_request = {.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
			 .revision = 0,
			 .response = nullptr};

} // namespace

namespace
{

__attribute__((used, section(".limine_requests_start"))) volatile std::uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) volatile std::uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

} // namespace

namespace
{

static void reserve_kernel_pages(void)
{
	// Walk the kernel page tables to find every physical page mapped in the
	// higher half (above 0xFFFF800000000000), then reserve those pages so
	// the buddy allocator never hands them out. This prevents corruption
	// when the bootloader scatters kernel BSS pages across USABLE regions.
	std::uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	auto* pml4 =
	    static_cast<std::uint64_t*>(memory::phys_to_virt(cr3 & ~0xFFFULL));

	std::uint64_t total_reserved = 0;

	// Walk each higher-half PML4 entry except 256 (HHDM maps everything)
	for (int pml4i = 257; pml4i < 512; pml4i++)
	{
		if ((pml4[pml4i] & 1) == 0)
			continue;

		auto* pdpt = static_cast<std::uint64_t*>(
		    memory::phys_to_virt(pml4[pml4i] & ~0xFFFULL));

		for (int pdpti = 0; pdpti < 512; pdpti++)
		{
			if ((pdpt[pdpti] & 1) == 0)
				continue;

			// 1 GiB page
			if (pdpt[pdpti] & (1 << 7))
			{
				std::uint64_t base =
				    (pdpt[pdpti] & ~0x3FFFFFULL);
				memory::buddy.reserve_range(
				    base, base + 0x40000000ULL);
				total_reserved += 0x40000000ULL / 4096;
				continue;
			}

			auto* pd = static_cast<std::uint64_t*>(
			    memory::phys_to_virt(pdpt[pdpti] & ~0xFFFULL));

			for (int pdi = 0; pdi < 512; pdi++)
			{
				if ((pd[pdi] & 1) == 0)
					continue;

				// 2 MiB page
				if (pd[pdi] & (1 << 7))
				{
					std::uint64_t base =
					    (pd[pdi] & ~0x1FFFFFULL);
					memory::buddy.reserve_range(
					    base, base + 0x200000ULL);
					total_reserved += 512;
					continue;
				}

				auto* pt = static_cast<std::uint64_t*>(
				    memory::phys_to_virt(pd[pdi] & ~0xFFFULL));

				// Reserve contiguous runs of 4K pages
				std::uint64_t run_start = 0;
				std::uint64_t run_end = 0;
				for (int pti = 0; pti < 512; pti++)
				{
					if ((pt[pti] & 1) == 0)
					{
						if (run_end > run_start)
						{
							memory::buddy
							    .reserve_range(
								run_start,
								run_end);
							total_reserved +=
							    (run_end -
							     run_start) /
							    4096;
							run_start = 0;
							run_end = 0;
						}
						continue;
					}

					std::uint64_t phys =
					    pt[pti] & ~0xFFFULL;
					if (run_end == 0)
					{
						run_start = phys;
						run_end = phys + 4096;
					}
					else if (phys == run_end)
					{
						run_end += 4096;
					}
					else
					{
						memory::buddy.reserve_range(
						    run_start, run_end);
						total_reserved +=
						    (run_end - run_start) /
						    4096;
						run_start = phys;
						run_end = phys + 4096;
					}
				}
				if (run_end > run_start)
				{
					memory::buddy.reserve_range(run_start,
								    run_end);
					total_reserved +=
					    (run_end - run_start) / 4096;
				}
			}
		}
	}

#ifdef DEBUG
	LOG("reserved_kernel_pages=%llu", total_reserved);
#endif
}

static std::uint8_t* alloc_boot_pages(int order, const char* name)
{
	std::uint64_t phys = memory::buddy.alloc_pages(order);
	if (!phys)
		PANIC("failed_to_alloc_%s", name);
	return static_cast<std::uint8_t*>(memory::phys_to_virt(phys));
}

void hcf(void)
{
	for (;;)
	{
		asm("hlt");
	}
}

} // namespace

extern "C" {
int __cxa_atexit(void (*)(void*), void*, void*)
{
	return 0;
}
void __cxa_pure_virtual(void)
{
	hcf();
}
void* __dso_handle;
}

extern void (*__init_array[])(void);
extern void (*__init_array_end[])(void);

extern "C" void kmain(void)
{
	smp::sse_enable();

	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
	{
		hcf();
	}

	for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++)
	{
		__init_array[i]();
	}

	logger::init();

	if (memmap_request.response == nullptr ||
	    memmap_request.response->entry_count < 1)
		PANIC("memmap_requst.response=nullptr");
	if (hhdm_request.response == nullptr)
		PANIC("hhdm_request.response=nullptr");
	if (rsdp_request.response == nullptr ||
	    rsdp_request.response->address == nullptr)
		PANIC("rsdp_request.response=nullptr");
	if (mp_request.response == nullptr)
		PANIC("mp_request.response=nullptr");
	if (exec_addr_request.response == nullptr)
		PANIC("exec_addr_request.response=nullptr");

	// memory

	memory::hhdm_offset = hhdm_request.response->offset;
#ifdef DEBUG
	LOG("hhdm_offset=0x%016llx", memory::hhdm_offset);
#endif
	std::uint64_t kernel_phys = 0;
	kernel_phys = exec_addr_request.response->physical_base;
#ifdef DEBUG
	LOG("kernel_phys=0x%016llx", kernel_phys);
#endif

	memory::buddy.init(memmap_request.response->entries,
			   memmap_request.response->entry_count, kernel_phys,
			   kernel_phys ? 0x4E000ULL : 0);

	reserve_kernel_pages();

	memory::buddy.log_stats();

	// gdt/idt

	static constexpr std::uint64_t KERNEL_STACK_SIZE = 16384;
	std::uint8_t* kernel_stack = alloc_boot_pages(2, "kernel_stack");

	static constexpr std::uint64_t IST_STACK_SIZE = 4096;
	std::uint8_t* ist1_stack = alloc_boot_pages(0, "ist1_stack");
	std::uint8_t* ist2_stack = alloc_boot_pages(0, "ist2_stack");
	std::uint8_t* ist3_stack = alloc_boot_pages(0, "ist3_stack");
	std::uint8_t* ist4_stack = alloc_boot_pages(0, "ist4_stack");

	gdt::gdt.init(KERNEL_STACK_SIZE, kernel_stack, IST_STACK_SIZE,
		      ist1_stack, ist2_stack, ist3_stack, ist4_stack);

	interrupts::idt::idt.init();
	__asm__("sti");

	// slub

	memory::slub.init();

	tests::run_all();

	// ACPI

	acpi::acpi.init(rsdp_request.response->address);

	// HPET (needed before LAPIC timer calibration)

	timers::hpet::hpet.init();

	auto boot_start_ticks = timers::hpet::hpet.read_counter();
	// PIC/APIC

	interrupts::apic::init_all();

	interrupts::apic::apic.timer_periodic(1000, 48);

	smp::wake_aps(mp_request.response);

	interrupts::apic::apic.enable_x2apic();
	interrupts::apic::apic.timer_periodic(1000, 48);

	tsc::tsc.init();

	sched::scheduler.init();

	pci::pci.init();

	drivers::storage::ahci::controller.init();

	// tests
	tests::run_all();

	auto boot_end_ticks = timers::hpet::hpet.read_counter();
	auto elapsed_ticks = boot_end_ticks - boot_start_ticks;
	auto period_fs = timers::hpet::hpet.get_freq();
	auto elapsed_ns = (elapsed_ticks * period_fs) / 1'000'000;
	LOG("Boot time: %llu ns (%llu ms)", elapsed_ns, elapsed_ns / 1'000'000);

	LOG("OH MY FUCKING GOD WE DID NOT TRIPPLE FAULT");

	hcf();
}
