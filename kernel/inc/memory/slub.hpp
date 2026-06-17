#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace memory
{

static constexpr std::uint64_t SLAB_MAGIC = 0xE5E5E5E5E5E5E5E5;
static constexpr std::uint64_t BUDDY_ALLOC_MAGIC = 0xB0B0B0B0B0B0B0B0;
static constexpr std::uint64_t KMALLOC_MAX_SLUB = 2048;

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
	Slab* slab_from_ptr(void* ptr);
	void slab_list_remove(Slab* slab);
	void slab_list_push(Slab* slab, bool is_partial);
	Slab* slab_alloc_new(SlubCache* cache);
	SlubCache* cache_for_size(std::uint64_t size);

public:
	void init(void);
	void* kmalloc(std::uint64_t size);
	void kfree(void* ptr);
};

extern SlubAllocator slub;

} // namespace memory
