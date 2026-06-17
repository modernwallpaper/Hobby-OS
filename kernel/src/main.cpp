#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <apic/ioapic.hpp>
#include <cstddef>
#include <cstdint>
#include <gdt/gdt.hpp>
#include <idt/idt.hpp>
#include <limine.h>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/slub.hpp>
#include <panic/panic.hpp>
#include <pic/pic.hpp>
#include <ports/ports.hpp>
#include <timers/hpet/hpet.hpp>

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
	if (rsdp_request.response == nullptr)
		PANIC("rsdp_request.response=nullptr");

	// memory

	memory::hhdm_offset = hhdm_request.response->offset;
#ifdef DEBUG
	LOG("hhdm_offset=0x%016llx", memory::hhdm_offset);
#endif
	memory::buddy.init(memmap_request.response->entries,
			   memmap_request.response->entry_count);

	memory::buddy.log_stats();

	// gdt/idt

	static constexpr std::uint64_t KERNEL_STACK_SIZE = 16384;
	std::uint8_t* kernel_stack = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(memory::buddy.alloc_pages(2)));

	static constexpr std::uint64_t IST_STACK_SIZE = 4096;
	std::uint8_t* ist1_stack = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(memory::buddy.alloc_pages(0)));
	std::uint8_t* ist2_stack = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(memory::buddy.alloc_pages(0)));
	std::uint8_t* ist3_stack = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(memory::buddy.alloc_pages(0)));
	std::uint8_t* ist4_stack = static_cast<std::uint8_t*>(
	    memory::phys_to_virt(memory::buddy.alloc_pages(0)));

	gdt::gdt.init(KERNEL_STACK_SIZE, kernel_stack, IST_STACK_SIZE,
		      ist1_stack, ist2_stack, ist3_stack, ist4_stack);

	interrupts::idt::idt.init();

	// slub

	memory::slub.init();

	// ACPI

	if (rsdp_request.response == nullptr ||
	    rsdp_request.response->address == nullptr)
	{
		LOG("no_rsdp_from_bootloader");
	}
	else
	{
		acpi::acpi.init(rsdp_request.response->address);
	}

	// HPET (needed before LAPIC timer calibration)

	timers::hpet::hpet.init();

	// PIC/APIC

	interrupts::apic::init_all();

	interrupts::apic::apic.timer_periodic(1000, 48);

	LOG("OH MY FUCKING GOD WE DID NOT TRIPPLE FAULT");
	asm("sti");
	hcf();
}
