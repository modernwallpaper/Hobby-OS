#pragma once

#include <cstdint>
#include <limine.h>
#include <panic/panic.hpp>
#include <sync/spinlock.hpp>

namespace memory
{
// minimum allocation unit: 4 KiB
static constexpr std::uint64_t PAGE_SIZE = 4096;

// highest buddy order: blocks of size PAGE_SIZE * 2^MAX_ORDER
// order 20 -> 4GiB per block
static constexpr int MAX_ORDER = 20;

// magic value written into each free block to detect double-free / corruption
static constexpr std::uint64_t FREE_MAGIC = 0xCAFEBABEDEADBEEF;

// higher half direct map offset provided by limine
// set during BuddyClass::init(); used to convert between phys / virt
extern std::uint64_t hhdm_offset;

// convert a physical address to a directly-mapped virtual address.
// limine maps every physical page at phys + hhdm_offset.
static inline void* phys_to_virt(std::uint64_t phys)
{
	if (__builtin_expect(hhdm_offset == 0, 0))
		PANIC("hddm_offset==0");
	return reinterpret_cast<void*>(phys + hhdm_offset);
}

// convert a HHDM virtual address back to its physical address
static inline std::uint64_t virt_to_phys(void* virt)
{
	auto v = reinterpret_cast<std::uint64_t>(virt);
	if (__builtin_expect(v < hhdm_offset, 0))
		PANIC("virt<hddm_offset");
	return v - hhdm_offset;
}

struct FreeBlock {
	FreeBlock* next;
	std::uint64_t magic;
	int order;
};

// maximum num of physical memory regions we track
static constexpr int MAX_REGIONS = 32;

struct MemRegion {
	std::uint64_t base; // first page-aligned physical address
	std::uint64_t top;  // first address past the region
};

class Buddy {
private:
	mutable sync::Spinlock lock;
	static constexpr std::uint64_t CANARY_VAL = 0xDEADBEEFCAFEBABE;
	std::uint64_t canary_before;
	FreeBlock* free_lists[MAX_ORDER + 1];
	std::uint64_t canary_after;
	std::uint64_t total_pages;
	int region_count;		// Number of valid entries in regions[]
	MemRegion regions[MAX_REGIONS]; // usable memory regions discovered
					// during init
	bool addr_is_usable(std::uint64_t phys) const;
	bool is_buddy_free(std::uint64_t phys, int order) const;
	void list_remove(std::uint64_t phys, int order);
	void insert_range(std::uint64_t base, std::uint64_t top);

public:
	void init(limine_memmap_entry** entries, std::uint64_t entry_count);
	std::uint64_t alloc_pages(int order);
	void free_page(std::uint64_t addr, int order);
	void log_stats(void) const;
};

extern Buddy buddy;

} // namespace memory
