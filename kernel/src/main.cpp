#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <apic/ioapic.hpp>
#include <block/block.hpp>
#include <cstddef>
#include <cstdint>
#include <drivers/storage/ahci.hpp>
#include <gdt/gdt.hpp>
#include <hpet/hpet.hpp>
#include <idt/idt.hpp>
#include <limine.h>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
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

__attribute__((
    used,
    section(".limine_requests"))) volatile limine_executable_file_request
    exec_file_request = {.id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
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

// Compute the kernel image's physical footprint (including BSS) by parsing the
// PT_LOAD program headers of the loaded ELF. Limine hands us the raw ELF
// buffer through the executable file request; the memory map does not
// necessarily exclude every BSS page from USABLE ranges, so the buddy
// allocator must reserve the exact range up front.
static std::uint64_t elf_kernel_size(const void* image,
				     std::uint64_t virtual_base)
{
	const auto* e = static_cast<const std::uint8_t*>(image);
	if (!e)
		return 0;

	// ELF magic, ELF64 class, little endian
	if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
		return 0;
	if (e[4] != 2 || e[5] != 1)
		return 0;

	std::uint64_t phoff = 0;
	for (int i = 0; i < 8; i++)
		phoff |= static_cast<std::uint64_t>(e[32 + i]) << (8 * i);
	std::uint16_t phentsize =
	    static_cast<std::uint16_t>(e[54]) | (static_cast<std::uint16_t>(e[55]) << 8);
	std::uint16_t phnum = static_cast<std::uint16_t>(e[56]) |
			      (static_cast<std::uint16_t>(e[57]) << 8);

	if (phentsize < 56 || phnum == 0)
		return 0;

	std::uint64_t end = 0;
	for (std::uint16_t i = 0; i < phnum; i++)
	{
		const std::uint8_t* ph =
		    e + phoff + static_cast<std::uint64_t>(i) * phentsize;

		std::uint32_t p_type = 0;
		for (int b = 0; b < 4; b++)
			p_type |= static_cast<std::uint32_t>(ph[b]) << (8 * b);
		if (p_type != 1) // PT_LOAD
			continue;

		std::uint64_t p_vaddr = 0, p_memsz = 0;
		for (int b = 0; b < 8; b++)
		{
			p_vaddr |= static_cast<std::uint64_t>(ph[16 + b]) << (8 * b);
			p_memsz |= static_cast<std::uint64_t>(ph[40 + b]) << (8 * b);
		}

		std::uint64_t seg_end = p_vaddr + p_memsz;
		if (seg_end > end)
			end = seg_end;
	}

	if (end <= virtual_base)
		return 0;

	std::uint64_t size = end - virtual_base;
	return (size + memory::PAGE_SIZE - 1) &
	       ~(static_cast<std::uint64_t>(memory::PAGE_SIZE) - 1);
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
	std::uint64_t kernel_virt = exec_addr_request.response->virtual_base;
#ifdef DEBUG
	LOG("kernel_phys=0x%016llx; kernel_virt=0x%016llx", kernel_phys,
	    kernel_virt);
#endif

	std::uint64_t kernel_size = 0;
	if (exec_file_request.response != nullptr &&
	    exec_file_request.response->executable_file != nullptr)
	{
		kernel_size = elf_kernel_size(
		    exec_file_request.response->executable_file->address,
		    kernel_virt);
	}
#ifdef DEBUG
	LOG("kernel_size=%llu_kib", kernel_size / 1024);
#endif

	memory::buddy.init(memmap_request.response->entries,
			   memmap_request.response->entry_count, kernel_phys,
			   kernel_size);

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

	// ACPI

	acpi::acpi.init(rsdp_request.response->address);

	// HPET (needed before LAPIC timer calibration)

	timers::hpet::hpet.init();

	auto boot_start_ticks = timers::hpet::hpet.read_counter();
	// PIC/APIC

	interrupts::apic::init_all();

	interrupts::apic::apic.timer_periodic(
	    1000, static_cast<std::uint8_t>(interrupts::idt::LAPIC_TIMER_VECTOR));

	smp::wake_aps(mp_request.response);

	interrupts::apic::apic.enable_x2apic();
	interrupts::apic::apic.timer_periodic(
	    1000, static_cast<std::uint8_t>(interrupts::idt::LAPIC_TIMER_VECTOR));

	tsc::tsc.init();

	sched::scheduler.init();

	pci::pci.init();

	drivers::storage::ahci::controller.init();

	// exercise the AHCI MSI/INTx interrupt path: read the boot disks first
	// sector and check for the MBR signature 0x55 0xAA at bytes 510/511
	std::uint8_t boot_sector[512];
	if (drivers::storage::ahci::controller.read_sector(0, 0, boot_sector))
	{
		LOG("ahci_read_sector_ok; first=0x%02x%02x%02x%02x; "
		    "mbr_sig=0x%02x%02x",
		    boot_sector[0], boot_sector[1], boot_sector[2],
		    boot_sector[3], boot_sector[510], boot_sector[511]);
	}
	else
	{
		LOG("ahci_read_sector_failed");
	}

	// exercise the block-layer write path: write a known pattern to the
	// final sector and read it back straight from the device
	block::device* disk = block::device_get(0);
	if (disk != nullptr && disk->block_count > 1)
	{
		std::uint8_t wbuf[512];
		std::uint8_t rbuf[512];
		for (int i = 0; i < 512; ++i)
			wbuf[i] = static_cast<std::uint8_t>(i);

		std::uint64_t last = disk->block_count - 1;
		bool ok = disk->write(disk, last, 1, wbuf) &&
			  disk->raw_read(disk, last, 1, rbuf) &&
			  memory::memcmp(wbuf, rbuf, 512) == 0;
		LOG("ahci_write_read_%s; blocks=%llu; last=%llu",
		    ok ? "ok" : "failed",
		    static_cast<std::uint64_t>(disk->block_count),
		    static_cast<std::uint64_t>(last));
	}
	else
	{
		LOG("ahci_block_device_unavailable");
	}

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
