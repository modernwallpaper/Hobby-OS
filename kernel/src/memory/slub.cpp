#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <memory/slub.hpp>
#include <panic/panic.hpp>

namespace memory
{

SlubAllocator slub;

std::uint64_t SlubAllocator::align_up(std::uint64_t x, std::uint64_t align)
{
	return (x + align - 1) & ~(align - 1);
}

void SlubAllocator::poison_object(void* obj, std::uint64_t obj_size)
{
	if (obj_size > sizeof(void*))
		memory::memset(static_cast<std::uint8_t*>(obj) + sizeof(void*),
			       SLAB_POISON, obj_size - sizeof(void*));
}

void SlubAllocator::verify_poison(void* obj, std::uint64_t obj_size)
{
	if (obj_size <= sizeof(void*))
		return;

	std::uint8_t* body =
	    static_cast<std::uint8_t*>(obj) + sizeof(void*);
	for (std::uint64_t i = 0; i < obj_size - sizeof(void*); i++)
	{
		if (body[i] != SLAB_POISON)
		{
			PANIC("slub_poison_corrupted; ptr=%p; "
			      "obj_size=%llu; offset=%llu; "
			      "found=0x%02x; use_after_free",
			      obj, obj_size, i, body[i]);
		}
	}
}

// validate that a pointer is a properly aligned object inside the slab
void SlubAllocator::check_object(Slab* slab, void* ptr)
{
	SlubCache* cache = slab->cache;
	std::uint64_t header_size = this->align_up(sizeof(Slab), 8);

	std::uint64_t slab_base = reinterpret_cast<std::uint64_t>(slab);
	std::uint64_t ptr_addr = reinterpret_cast<std::uint64_t>(ptr);

	if (ptr_addr < slab_base + header_size ||
	    ptr_addr + cache->obj_size > slab_base + PAGE_SIZE)
	{
		PANIC("slub_free_out_of_bounds; ptr=%p; slab=%p; "
		      "obj_size=%llu",
		      ptr, slab, cache->obj_size);
	}

	std::uint64_t obj_off = ptr_addr - (slab_base + header_size);
	if (obj_off % cache->obj_size != 0)
	{
		PANIC("slub_free_misaligned; ptr=%p; slab=%p; "
		      "obj_size=%llu; offset=%llu",
		      ptr, slab, cache->obj_size, obj_off);
	}
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
		PANIC("bad_magic; slab=%p; ptr=%p", slab, ptr);
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
		this->poison_object(obj_ptr, cache->obj_size);
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
		PANIC("slub_free_list_null; cache_obj_size=%llu; "
		      "inuse=%u; total=%u; slab_corrupted",
		      cache->obj_size, slab->inuse, slab->total);
	}

	// the object was poisoned on free; if the pattern is gone someone
	// wrote to it after it was freed
	this->verify_poison(obj, cache->obj_size);

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

void* SlubAllocator::kcalloc(std::uint64_t n, std::uint64_t size)
{
	std::uint64_t total = n * size;
	void* ptr = this->kmalloc(total);
	if (ptr)
		memory::memset(ptr, 0, total);
	return ptr;
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

		// the caller must hand back the exact pointer kmalloc()
		// returned; interior pointers would free the wrong range
		std::uint64_t expected =
		    reinterpret_cast<std::uint64_t>(page_base) +
		    sizeof(BuddyAllocHeader);
		if (reinterpret_cast<std::uint64_t>(ptr) != expected)
		{
			PANIC("slub_free_misaligned_large; ptr=%p; "
			      "expected=%p",
			      ptr,
			      reinterpret_cast<void*>(expected));
		}

		if (hdr->order < 0 || hdr->order > MAX_ORDER)
		{
			PANIC("slub_free_bad_order; ptr=%p; order=%d", ptr,
			      hdr->order);
		}

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

	SlubCache* cache = slab->cache;
	if (cache == nullptr)
	{
		PANIC("slub_free_null_cache; ptr=%p; slab=%p", ptr, slab);
	}

	this->check_object(slab, ptr);

	if (slab->inuse == 0)
	{
		PANIC("slub_double_free; ptr=%p; slab=%p; inuse=0", ptr,
		      slab);
	}

	// detect double frees by scanning the slab's free list. bound the
	// walk by the number of live free objects so a corrupt cycle can
	// never spin forever.
	std::uint16_t max_free = slab->total - slab->inuse;
	std::uint16_t walked = 0;
	for (void* f = slab->free_list; f != nullptr;
	     f = *reinterpret_cast<void**>(f))
	{
		if (f == ptr)
		{
			PANIC("slub_double_free; ptr=%p; slab=%p", ptr,
			      slab);
		}
		if (++walked > max_free)
		{
			PANIC("slub_free_list_corrupt; ptr=%p; slab=%p; "
			      "cycle_or_overflow",
			      ptr, slab);
		}
	}

	this->poison_object(ptr, cache->obj_size);
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
