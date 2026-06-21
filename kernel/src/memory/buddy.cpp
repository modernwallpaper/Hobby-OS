#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <panic/panic.hpp>

namespace memory
{

std::uint64_t hhdm_offset = 0;
Buddy buddy;

void Buddy::init(limine_memmap_entry** entries, std::uint64_t entry_count,
		 std::uint64_t reserve_phys_base,
		 std::uint64_t reserve_phys_size)
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
	std::uint64_t reserve_end = reserve_phys_base + reserve_phys_size;

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

		// Exclude the reserved (kernel) range from this region
		if (reserve_phys_size > 0 && rbase < reserve_end &&
		    rtop > reserve_phys_base)
		{
			// Split into up to two regions around the reservation
			if (rbase < reserve_phys_base)
			{
				this->regions[this->region_count].base = rbase;
				this->regions[this->region_count].top =
				    reserve_phys_base;
				this->region_count++;
			}
			if (rtop > reserve_end)
			{
				this->regions[this->region_count].base =
				    reserve_end;
				this->regions[this->region_count].top = rtop;
				this->region_count++;
			}
		}
		else
		{
			this->regions[this->region_count].base = rbase;
			this->regions[this->region_count].top = rtop;
			this->region_count++;
		}
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
		PANIC("buddy_canary_corrupted; before=0x%016llx; "
		      "after=0x%016llx; order=%d",
		      this->canary_before, this->canary_after, order);
	}

	if (order < 0 || order > MAX_ORDER)
	{
		this->lock.unlock();
		return 0;
	}

	FreeBlock* block;
	int current;

	// Retry loop: skip corrupted free-list entries (the bootloader may
	// have placed kernel image pages inside USABLE memory-map ranges).
	for (;;)
	{
		// find the first order that has a free block
		current = order;
		while (current <= MAX_ORDER &&
		       this->free_lists[current] == nullptr)
			current++;

		if (current > MAX_ORDER)
			PANIC("alloc_pages=%d; out_of_memory", order);

		block = this->free_lists[current];

		// Validate the pointer before dereferencing
		std::uint64_t block_val =
		    reinterpret_cast<std::uint64_t>(block);
		if ((block_val >> 47) != 0 && (block_val >> 47) != 0x1FFFF)
		{
			PANIC("non-canonical free_lists[%d] = %p; "
			      "order=%d",
			      current, block, order);
		}

		if (block->magic == FREE_MAGIC)
			break; // valid block

		// Corrupted block — remove from list and retry
		FreeBlock* next = block->next;
		if (next)
		{
			std::uint64_t nv =
			    reinterpret_cast<std::uint64_t>(next);
			if ((nv >> 47) != 0 && (nv >> 47) != 0x1FFFF)
				next = nullptr; // next pointer also corrupted
		}
		this->free_lists[current] = next;
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
		PANIC("misaligned_addr=0x%016llx", addr);
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

void Buddy::reserve_range(std::uint64_t base, std::uint64_t top)
{
	this->lock.lock();

	base &= ~(PAGE_SIZE - 1);
	top = (top + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	if (base >= top)
	{
		this->lock.unlock();
		return;
	}

	// Walk orders from high to low so we handle large blocks first
	for (int order = MAX_ORDER; order >= 0; order--)
	{
		std::uint64_t block_size = PAGE_SIZE * (1ULL << order);

		FreeBlock** prev = &this->free_lists[order];
		FreeBlock* curr = this->free_lists[order];

		while (curr)
		{
			std::uint64_t block_phys = virt_to_phys(curr);
			std::uint64_t block_end = block_phys + block_size;

			if (block_end <= base || block_phys >= top)
			{
				// No overlap
				prev = &curr->next;
				curr = curr->next;
				continue;
			}

			// This block overlaps the reserved range
			*prev = curr->next;
			this->total_pages -= (1ULL << order);

			if (order == 0)
			{
				// order 0: just remove it (page is reserved)
				curr = *prev;
				continue;
			}

			// Split the block into two halves of order-1
			int lower = order - 1;
			std::uint64_t lower_size = PAGE_SIZE * (1ULL << lower);

			// Re-insert both halves at the lower order
			FreeBlock* first =
			    static_cast<FreeBlock*>(phys_to_virt(block_phys));
			first->magic = FREE_MAGIC;
			first->order = lower;
			first->next = this->free_lists[lower];
			this->free_lists[lower] = first;

			FreeBlock* second = static_cast<FreeBlock*>(
			    phys_to_virt(block_phys + lower_size));
			second->magic = FREE_MAGIC;
			second->order = lower;
			second->next = this->free_lists[lower];
			this->free_lists[lower] = second;

			// The halves will be processed when the outer loop
			// reaches order-1 — any half that still overlaps the
			// reserved range will be split further or removed
			curr = *prev;
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
