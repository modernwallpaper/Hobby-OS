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
	std::uint64_t virt = memory::buddy.alloc_pages(order);
	dma_spinlock.unlock_restore(flags);
	if (!virt)
	{
#ifdef DEBUG
		LOG("oom; bytes=%u; pages=%u; order=%u", size, pages, order);
#endif
		return {nullptr, 0};
	}
	addr result;
	result.virt = reinterpret_cast<void*>(virt);
	result.phys = memory::virt_to_phys(reinterpret_cast<void*>(virt));
	return result;
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
		PANIC("invaled_free_order_for_size; order=%d; size=%u");
	}
	std::uint64_t flags;
	dma_spinlock.lock_save(flags);
	memory::buddy.free_page(reinterpret_cast<std::uint64_t>(address.virt),
				order);
	dma_spinlock.unlock_restore(flags);
}

} // namespace dma
