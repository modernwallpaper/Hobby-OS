#include <dma/dma.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <sync/spinlock.hpp>

namespace dma
{

sync::IrqSpinlock dma_spinlock;

addr alloc(std::size_t size)
{
	if (size == 0)
		return {nullptr, 0};
	std::size_t pages = (size + memory::PAGE_SIZE - 1) / memory::PAGE_SIZE;
	int order = 0;
	while ((1ULL << order) < pages)
		++order;

	if (order >= memory::MAX_ORDER)
	{
#ifdef DEBUG
		LOG("alloc_too_large; bytes=%u; pages=%u", size, pages);
#endif
		return {nullptr, 0};
	}

	std::uint64_t flags;
	dma_spinlock.lock_save(flags);
	std::uint64_t phys = memory::buddy.alloc_pages(order);
	dma_spinlock.unlock_restore(flags);
	if (!phys)
	{
#ifdef DEBUG
		LOG("oom; bytes=%u; pages=%u; order=%u", size, pages, order);
#endif
		return {nullptr, 0};
	}
	addr result;
	result.virt = memory::phys_to_virt(phys);
	result.phys = phys;
	return result;
}

// L1 data cache line size, queried once via CPUID leaf 1 (EBX[15:8])
static std::size_t cache_line_size(void)
{
	static std::size_t line = 0;
	if (line != 0)
		return line;
	std::uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
	__asm__ volatile("cpuid"
			 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			 : "a"(1));
	line = static_cast<std::size_t>((ebx >> 8) & 0xFF);
	if (line == 0)
		line = 64; // fall back to the common line size
	return line;
}

// clflush every cache line covering [start, start+size). clflush writes back
// dirty data and invalidates the line, which is exactly the write-back /
// invalidate primitive needed for coherent x86 DMA.
static void clflush_range(std::uintptr_t start, std::size_t size)
{
	std::size_t line = cache_line_size();
	std::uintptr_t end = start + size;
	for (std::uintptr_t a = start & ~(line - 1); a < end; a += line)
		__asm__ volatile("clflush (%0)" : : "r"(a) : "memory");
	__asm__ volatile("mfence" : : : "memory");
}

void cache_flush(void* addr, std::size_t size)
{
	if (addr == nullptr || size == 0)
		return;
	clflush_range(reinterpret_cast<std::uintptr_t>(addr), size);
}

void cache_invalidate(void* addr, std::size_t size)
{
	if (addr == nullptr || size == 0)
		return;
	clflush_range(reinterpret_cast<std::uintptr_t>(addr), size);
}

void free(addr address, std::size_t size)
{
	if (!address.virt || size == 0)
		return;
	std::size_t pages = (size + memory::PAGE_SIZE - 1) / memory::PAGE_SIZE;
	int order = 0;
	while ((1ULL << order) < pages)
		++order;
	if (order >= memory::MAX_ORDER)
	{
		PANIC("invaled_free_order_for_size; order=%d; size=%u", order, size);
	}
	std::uint64_t flags;
	dma_spinlock.lock_save(flags);
	memory::buddy.free_page(address.phys, order);
	dma_spinlock.unlock_restore(flags);
}

} // namespace dma
