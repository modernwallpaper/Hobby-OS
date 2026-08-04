#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace memory
{

static constexpr std::uint64_t SLAB_MAGIC = 0xE5E5E5E5E5E5E5E5;
static constexpr std::uint64_t BUDDY_ALLOC_MAGIC = 0xB0B0B0B0B0B0B0B0;
static constexpr std::uint64_t KMALLOC_MAX_SLUB = 2048;

// Alignment contract for kmalloc()/kcalloc() return values:
//   - SLUB objects start at slab_base + align_up(sizeof(Slab), 8) (48 bytes)
//     on a page-aligned base and advance by the cache's obj_size, which is
//     always a multiple of 8. The 8-byte cache therefore hands out 8-aligned
//     objects; every other cache (16..2048) starts at a 16-aligned offset and
//     steps by a multiple of 16, so those objects are 16-byte aligned.
//   - Buddy-backed large allocations (size > KMALLOC_MAX_SLUB) return
//     page + sizeof(BuddyAllocHeader) (16 bytes), i.e. 16-byte aligned.
//   In short: kmalloc() guarantees at least 8-byte alignment, and 16-byte
//   alignment for any request of 16 bytes or more. Callers that need 16-byte
//   (or 8-byte) alignment can use the return value directly; anything stricter
//   requires the caller to over-allocate and align by hand.
static constexpr std::uint64_t KMALLOC_MIN_ALIGN = 8;
static constexpr std::uint64_t KMALLOC_ALIGN_16_FROM = 16;

// byte pattern written into the body of a freed SLUB object. The first 8
// bytes are the free-list link, so only bytes [8, obj_size) are poisoned.
// kmalloc() verifies the pattern is intact before handing the object out,
// which catches writes to memory that has already been freed.
static constexpr std::uint8_t SLAB_POISON = 0xCD;

struct SlubCache;

struct Slab {
	std::uint64_t slab_magic;
	Slab* next;
	Slab* prev;
	void* free_list;
	SlubCache* cache;
	std::uint16_t inuse;
	std::uint16_t total;
};

struct SlubCache {
	const char* name;
	std::uint64_t obj_size;
	std::uint64_t objs_per_slab;
	Slab* partial;
	Slab* full;
};

struct BuddyAllocHeader {
	std::uint64_t magic;
	int order;
};

static constexpr std::uint64_t KMALLOC_SIZES[] = {8,   16,  32,	  64,  128,
						  256, 512, 1024, 2048};
static constexpr int KMALLOC_NUM_CACHES = 9;

class SlubAllocator {
private:
	mutable sync::Spinlock lock;
	SlubCache kmalloc_caches[KMALLOC_NUM_CACHES];

	static std::uint64_t align_up(std::uint64_t x, std::uint64_t align);
	static void poison_object(void* obj, std::uint64_t obj_size);
	static void verify_poison(void* obj, std::uint64_t obj_size);
	Slab* slab_from_ptr(void* ptr);
	void slab_list_remove(Slab* slab);
	void slab_list_push(Slab* slab, bool is_partial);
	Slab* slab_alloc_new(SlubCache* cache);
	SlubCache* cache_for_size(std::uint64_t size);
	void check_object(Slab* slab, void* ptr);

public:
	void init(void);
	void* kmalloc(std::uint64_t size);
	void* kcalloc(std::uint64_t n, std::uint64_t size);
	void kfree(void* ptr);
};

extern SlubAllocator slub;

} // namespace memory
