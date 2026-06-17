#include <logging/logger.hpp>
#include <panic/panic.hpp>
#include <memory/buddy.hpp>
#include <memory/slub.hpp>

namespace memory
{

SlubAllocator slub;

std::uint64_t SlubAllocator::align_up(std::uint64_t x, std::uint64_t align)
{
	return (x + align - 1) & ~(align - 1);
}

// locate the slab header from an object pointer
Slab* SlubAllocator::slab_from_ptr(void* ptr)
{
	if (!ptr)
		return nullptr;

	std::uint64_t page_base =
	    reinterpret_cast<std::uint64_t>(ptr) & ~(PAGE_SIZE - 1);
	Slab* slab = reinterpret_cast<Slab*>(page_base);

	if (slab->slab_magic != SLAB_MAGIC)
	{
		PANIC("kfree; bad_magic; slab=%p; ptr=%p", slab, ptr);
	}
	return slab;
}

// unlink a slab from its cache's partial/full list
void SlubAllocator::slab_list_remove(Slab* slab)
{
	if (slab->next)
		slab->next->prev = slab->prev;
	if (slab->prev)
		slab->prev->next = slab->next;
	else
	{
		SlubCache* cache = slab->cache;
		if (slab->inuse == slab->total)
			cache->full = slab->next;
		else
			cache->partial = slab->next;
	}
}

// link a slab into its cache's partial or full list
void SlubAllocator::slab_list_push(Slab* slab, bool is_partial)
{
	SlubCache* cache = slab->cache;
	Slab** head = is_partial ? &cache->partial : &cache->full;

	slab->next = *head;
	slab->prev = nullptr;
	if (*head)
		(*head)->prev = slab;
	*head = slab;
}

// allocate a new slab from the page allocator and init its free list
Slab* SlubAllocator::slab_alloc_new(SlubCache* cache)
{
	std::uint64_t phys = buddy.alloc_pages(0);
	if (!phys)
		return nullptr;

	Slab* slab = static_cast<Slab*>(phys_to_virt(phys));

	std::uint64_t header_size = this->align_up(sizeof(Slab), 8);

	slab->slab_magic = SLAB_MAGIC;
	slab->next = nullptr;
	slab->prev = nullptr;
	slab->cache = cache;
	slab->inuse = 0;
	slab->total = static_cast<std::uint16_t>((PAGE_SIZE - header_size) /
						 cache->obj_size);

	std::uint8_t* obj_ptr =
	    reinterpret_cast<std::uint8_t*>(slab) + header_size;
	void* head = nullptr;

	for (std::uint16_t i = 0; i < slab->total; i++)
	{
		*reinterpret_cast<void**>(obj_ptr) = head;
		head = obj_ptr;
		obj_ptr += cache->obj_size;
	}
	slab->free_list = head;

	return slab;
}

// find the smallest cache that fits the requested size
SlubCache* SlubAllocator::cache_for_size(std::uint64_t size)
{
	if (size < 8)
		size = 8;
	size = this->align_up(size, 8);

	for (int i = 0; i < KMALLOC_NUM_CACHES; i++)
	{
		if (this->kmalloc_caches[i].obj_size >= size)
			return &this->kmalloc_caches[i];
	}
	return nullptr;
}

void SlubAllocator::init(void)
{
#ifdef DEBUG
	LOG("num_caches=%d", KMALLOC_NUM_CACHES);
#endif

	for (int i = 0; i < KMALLOC_NUM_CACHES; i++)
	{
		std::uint64_t size = KMALLOC_SIZES[i];
		std::uint64_t hdr_size = this->align_up(sizeof(Slab), 8);

		this->kmalloc_caches[i].name = nullptr;
		this->kmalloc_caches[i].obj_size = size;
		this->kmalloc_caches[i].objs_per_slab =
		    (PAGE_SIZE - hdr_size) / size;
		this->kmalloc_caches[i].partial = nullptr;
		this->kmalloc_caches[i].full = nullptr;

#ifdef DEBUG
		LOG("cache=%d; obj_size=%llu; objs_per_slab=%llu", i, size,
		    this->kmalloc_caches[i].objs_per_slab);
#endif
	}
}

// allocate memory: small allocations use SLUB caches, large ones go to buddy
void* SlubAllocator::kmalloc(std::uint64_t size)
{
	this->lock.lock();

	if (size > KMALLOC_MAX_SLUB)
	{
		std::uint64_t total_needed = sizeof(BuddyAllocHeader) + size;
		std::uint64_t pages_needed =
		    (total_needed + PAGE_SIZE - 1) / PAGE_SIZE;

		int order = 0;
		while ((1ULL << order) < pages_needed)
			order++;

		std::uint64_t phys = buddy.alloc_pages(order);
		if (!phys)
		{
			PANIC("kmalloc=%llu; buddy_oom", size);
		}

		auto* hdr = static_cast<BuddyAllocHeader*>(phys_to_virt(phys));
		hdr->magic = BUDDY_ALLOC_MAGIC;
		hdr->order = order;

		void* ret =
		    static_cast<void*>(reinterpret_cast<std::uint8_t*>(hdr) +
				       sizeof(BuddyAllocHeader));
		this->lock.unlock();
		return ret;
	}

	SlubCache* cache = this->cache_for_size(size);
	if (!cache)
	{
		PANIC("kmalloc=%llu; no_suitable_cache", size);
	}

	Slab* slab = cache->partial;

	if (!slab)
	{
		slab = this->slab_alloc_new(cache);
		if (!slab)
		{
			PANIC("kmalloc=%llu; oom", size);
		}
		this->slab_list_push(slab, true);
	}

	void* obj = slab->free_list;
	if (!obj)
	{
#ifdef DEBUG
		LOG("kmalloc=%llu; slab_corrupted; free_list=null; allocating_new_slab",
		    size);
#endif
		slab = this->slab_alloc_new(cache);
		if (!slab)
		{
			this->lock.unlock();
			return nullptr;
		}
		this->slab_list_push(slab, true);
		obj = slab->free_list;
	}

	slab->free_list = *reinterpret_cast<void**>(obj);
	slab->inuse++;

	if (slab->inuse == slab->total)
	{
		this->slab_list_remove(slab);
		this->slab_list_push(slab, false);
	}

	this->lock.unlock();
	return obj;
}

// free memory, detecting SLUB vs buddy allocation via header magic
void SlubAllocator::kfree(void* ptr)
{
	if (!ptr)
		return;

	this->lock.lock();

	void* page_base = reinterpret_cast<void*>(
	    reinterpret_cast<std::uint64_t>(ptr) & ~(PAGE_SIZE - 1));
	if (*static_cast<std::uint64_t*>(page_base) == BUDDY_ALLOC_MAGIC)
	{
		auto* hdr = static_cast<BuddyAllocHeader*>(page_base);
		buddy.free_page(virt_to_phys(page_base), hdr->order);
		this->lock.unlock();
		return;
	}

	Slab* slab = this->slab_from_ptr(ptr);
	if (!slab)
	{
		this->lock.unlock();
		return;
	}

	*reinterpret_cast<void**>(ptr) = slab->free_list;
	slab->free_list = ptr;
	slab->inuse--;

	std::uint16_t was_used = slab->inuse + 1;

	if (was_used == slab->total && slab->inuse < slab->total)
	{
		this->slab_list_remove(slab);
		this->slab_list_push(slab, true);
	}

	if (slab->inuse == 0)
	{
		this->slab_list_remove(slab);

		std::uint64_t phys = virt_to_phys(slab);
		buddy.free_page(phys, 0);
	}

	this->lock.unlock();
}

} // namespace memory
