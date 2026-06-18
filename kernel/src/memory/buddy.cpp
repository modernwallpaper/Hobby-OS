#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <panic/panic.hpp>

namespace memory
{

std::uint64_t hhdm_offset = 0;
Buddy buddy;

void Buddy::init(limine_memmap_entry** entries, std::uint64_t entry_count)
{
	this->canary_before = CANARY_VAL;
	this->canary_after = CANARY_VAL;

	// clear all free lists
	for (int i = 0; i <= MAX_ORDER; i++)
		this->free_lists[i] = nullptr;

	this->total_pages = 0;
	this->region_count = 0;

	// skip first 2 MiB (bootloader/bios stuff)
	std::uint64_t safe_base = 2 * 1024 * 1024;

	for (std::uint64_t i = 0;
	     i < entry_count && this->region_count < MAX_REGIONS; i++)
	{
		auto e = entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		std::uint64_t rbase =
		    (e->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
		std::uint64_t rtop = (e->base + e->length) & ~(PAGE_SIZE - 1);

		if (rbase >= rtop)
			continue;

		// apply safe_base
		if (rtop <= safe_base)
			continue;
		if (rbase < safe_base)
			rbase = safe_base;

		this->regions[this->region_count].base = rbase;
		this->regions[this->region_count].top = rtop;
		this->region_count++;
	}

	for (int ri = 0; ri < this->region_count; ri++)
	{
		std::uint64_t rbase = this->regions[ri].base;
		std::uint64_t rtop = this->regions[ri].top;
#ifdef DEBUG
		LOG("buddy_region=%d; range=0x%016llx-0x%016llx; "
		    "space=%llu_mib",
		    ri, rbase, rtop, (rtop - rbase) / (1024 * 1024));
#endif

		this->insert_range(rbase, rtop);
	}

#ifdef DEBUG
	LOG("amount_pages=~%llu; available_space=%llu_mib", this->total_pages,
	    (this->total_pages * PAGE_SIZE) / (1024 * 1024));
#endif
}

// allocate a block of 2^order contiguous pages
std::uint64_t Buddy::alloc_pages(int order)
{
	this->lock.lock();

	if (this->canary_before != CANARY_VAL ||
	    this->canary_after != CANARY_VAL)
	{
		PANIC(
		    "buddy_canary_corrupted; before=0x%016llx; after=0x%016llx; order=%d",
		    this->canary_before, this->canary_after, order);
	}

	if (order < 0 || order > MAX_ORDER)
	{
		this->lock.unlock();
		return 0;
	}

	// find the first order that has a free block
	int current = order;
	while (current <= MAX_ORDER && this->free_lists[current] == nullptr)
		current++;

	if (current > MAX_ORDER)
	{
		PANIC("alloc_pages=%d; out_of_memory", order);
	}

	// pop the head of the list
	FreeBlock* block = this->free_lists[current];

	// Validate the pointer before dereferencing — a non-canonical address
	// would cause a #GP (vector 13) rather than a #PF.
	std::uint64_t block_val = reinterpret_cast<std::uint64_t>(block);
	if ((block_val >> 47) != 0 && (block_val >> 47) != 0x1FFFF)
	{
		PANIC(
		    "alloc_pages; non-canonical free_lists[%d] = %p; order=%d; canary_before=0x%016llx; canary_after=0x%016llx",
		    current, block, order, this->canary_before,
		    this->canary_after);
	}
	if (block->magic != FREE_MAGIC)
	{
		PANIC(
		    "alloc_pages; bad_magic in free_lists[%d] = %p; magic=0x%016llx (expected 0x%016llx); order=%d",
		    current, block, block->magic, FREE_MAGIC, order);
	}

	this->free_lists[current] = block->next;

	std::uint64_t phys = virt_to_phys(block);

	// split the block down to the requested order
	while (current > order)
	{
		current--;
		// size of a block at the lower order
		std::uint64_t block_size = PAGE_SIZE * (1ULL << current);
		std::uint64_t buddy_phys = phys + block_size;

		// insert the buddy into the current-order free list
		FreeBlock* buddy =
		    static_cast<FreeBlock*>(phys_to_virt(buddy_phys));
		buddy->magic = FREE_MAGIC;
		buddy->order = current;
		buddy->next = this->free_lists[current];
		this->free_lists[current] = buddy;
	}

	this->lock.unlock();
	return phys;
}

// free a block and coalesce with its buddies
void Buddy::free_page(std::uint64_t addr, int order)
{
	this->lock.lock();

	if (order < 0 || order > MAX_ORDER)
	{
		this->lock.unlock();
		return;
	}
	if ((addr & (PAGE_SIZE - 1)) != 0)
	{
		PANIC("free_page; misaligned_addr=0x%016llx", addr);
	}

	std::uint64_t phys = addr;

	while (order < MAX_ORDER)
	{
		// XOR trick: buddy at same order flips bit "order + 12"
		std::uint64_t buddy_phys = phys ^ (1ULL << (order + 12));

		if (!this->addr_is_usable(buddy_phys))
			break;

		if (!this->is_buddy_free(buddy_phys, order))
			break;

		this->list_remove(buddy_phys, order);

		if (buddy_phys < phys)
			phys = buddy_phys;

		order++;
	}

	// insert into the free list for the final order
	FreeBlock* block = static_cast<FreeBlock*>(phys_to_virt(phys));
	block->magic = FREE_MAGIC;
	block->order = order;
	block->next = this->free_lists[order];
	this->free_lists[order] = block;

	this->lock.unlock();
}

void Buddy::log_stats(void) const
{
	this->lock.lock();

	for (int i = 0; i <= MAX_ORDER; i++)
	{
		std::uint64_t count = 0;
		for (FreeBlock* b = this->free_lists[i]; b != nullptr;
		     b = b->next)
			count++;
		if (count > 0)
		{
#ifdef DEBUG
			std::uint64_t block_size = PAGE_SIZE * (1ULL << i);
			LOG("order=%2d; size=%4llu_kib; blocks=%llu", i,
			    block_size / 1024, count);
#endif
		}
	}

	this->lock.unlock();
}

// break a contiguous range into largest aligned free blocks
void Buddy::insert_range(std::uint64_t base, std::uint64_t top)
{
	while (base < top)
	{
		// find the largest order whose block fits and is aligned
		int order = 0;
		while (order < MAX_ORDER)
		{
			std::uint64_t block_size = PAGE_SIZE * (1ULL << order);
			if (block_size > (top - base))
				break;
			if (base & (block_size - 1))
				break;
			order++;
		}
		order--; // last valid order

		std::uint64_t block_size = PAGE_SIZE * (1ULL << order);

		FreeBlock* block = static_cast<FreeBlock*>(phys_to_virt(base));
		block->magic = FREE_MAGIC;
		block->order = order;
		block->next = this->free_lists[order];
		this->free_lists[order] = block;

		this->total_pages += (1ULL << order);
		base += block_size;
	}
}

// check if a physical address falls inside a tracked region
bool Buddy::addr_is_usable(std::uint64_t phys) const
{
	for (int i = 0; i < this->region_count; i++)
	{
		if (phys >= this->regions[i].base &&
		    phys < this->regions[i].top)
			return true;
	}
	return false;
}

bool Buddy::is_buddy_free(std::uint64_t phys, int order) const
{
	FreeBlock* block = static_cast<FreeBlock*>(phys_to_virt(phys));
	return block->magic == FREE_MAGIC && block->order == order;
}

void Buddy::list_remove(std::uint64_t phys, int order)
{
	FreeBlock** prev = &this->free_lists[order];
	FreeBlock* curr = this->free_lists[order];

	while (curr)
	{
		if (virt_to_phys(curr) == phys)
		{
			*prev = curr->next;
			return;
		}
		prev = &curr->next;
		curr = curr->next;
	}
}

} // namespace memory
