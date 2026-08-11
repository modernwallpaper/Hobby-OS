#include <block/block.hpp>
#include <logging/logger.hpp>
#include <memory/memory.hpp>
#include <memory/slub.hpp>
#include <sync/spinlock.hpp>

namespace block
{

static sync::IrqSpinlock block_lock;
static device** block_devices = nullptr;
static int block_count = 0;
static int block_capacity = 0;

static constexpr int BLOCK_INIT_CAP = 8;
static constexpr int BLOCK_CACHE_ENTRIES = 64;

struct block_cache_entry {
	bool valid;
	std::uint64_t lba;
	std::uint8_t* data;
};

struct block_cache {
	sync::IrqSpinlock lock;
	std::uint32_t block_size;
	std::uint32_t next_evict;
	block_cache_entry entries[BLOCK_CACHE_ENTRIES];
};

static bool range_fits(device* dev, std::uint64_t lba, std::uint32_t count)
{
	if (count == 0)
		return true;
	if (lba >= dev->block_count)
		return false;
	return count <= dev->block_count - lba;
}

static block_cache* cache_create(std::uint32_t block_size)
{
	if (block_size == 0)
		return nullptr;

	auto* cache = static_cast<block_cache*>(
	    memory::slub.kcalloc(1, sizeof(block_cache)));
	if (!cache)
		return nullptr;
	cache->block_size = block_size;
	for (int i = 0; i < BLOCK_CACHE_ENTRIES; ++i)
	{
		cache->entries[i].data = static_cast<std::uint8_t*>(
		    memory::slub.kmalloc(block_size));
		if (!cache->entries[i].data)
		{
	for (int j = 0; j < i; ++j)
		memory::slub.kfree(cache->entries[j].data);
			memory::slub.kfree(cache);
			return nullptr;
		}
	}

	return cache;
}

static int cache_find_locked(block_cache* cache, std::uint64_t lba)
{
	for (int i = 0; i < BLOCK_CACHE_ENTRIES; ++i)
	{
		if (cache->entries[i].valid && cache->entries[i].lba == lba)
			return i;
	}
	return -1;
}

static void cache_store(block_cache* cache, std::uint64_t lba, const void* data)
{
	std::uint64_t flags;
	cache->lock.lock_save(flags);
	int idx = cache_find_locked(cache, lba);
	if (idx < 0)
	{
		idx = cache->next_evict;
		cache->next_evict =
		    (cache->next_evict + 1) % BLOCK_CACHE_ENTRIES;
		cache->entries[idx].valid = true;
		cache->entries[idx].lba = lba;
	}

	memory::memcpy(cache->entries[idx].data, data, cache->block_size);
	cache->lock.unlock_restore(flags);
}

static bool cached_read_one(device* dev, std::uint64_t lba, std::uint8_t* dst)
{
	auto* cache = static_cast<block_cache*>(dev->cache);
	if (!cache || !dev->raw_read)
		return dev->raw_read ? dev->raw_read(dev, lba, 1, dst) : false;

	std::uint64_t flags;
	cache->lock.lock_save(flags);
	int idx = cache_find_locked(cache, lba);
	if (idx >= 0)
	{
		memory::memcpy(dst, cache->entries[idx].data,
			       cache->block_size);
		cache->lock.unlock_restore(flags);
		return true;
	}
	cache->lock.unlock_restore(flags);

	std::uint8_t* tmp =
	    static_cast<std::uint8_t*>(memory::slub.kmalloc(cache->block_size));
	if (!tmp)
		return dev->raw_read(dev, lba, 1, tmp);

	if (!dev->raw_read(dev, lba, 1, tmp))
	{
		memory::slub.kfree(tmp);
		return false;
	}
	memory::memcpy(dst, tmp, cache->block_size);
	cache_store(cache, lba, tmp);
	memory::slub.kfree(tmp);
	return true;
}

static bool cached_read(device* dev, std::uint64_t lba, std::uint32_t count,
			void* buf)
{
	if (count == 0)
		return true;
	if (!dev || !buf || !dev->raw_read || !range_fits(dev, lba, count))
		return false;
	auto* dst = static_cast<std::uint8_t*>(buf);
	std::uint32_t block_size = dev->block_size;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (!cached_read_one(dev, lba + i, dst + i * block_size))
			return false;
	}
	return true;
}

static bool cached_write(device* dev, std::uint64_t lba, std::uint32_t count,
			 const void* buf)
{
	if (count == 0)
		return true;
	if (!dev || !buf || !dev->raw_write || !range_fits(dev, lba, count))
		return false;
	if (!dev->raw_write(dev, lba, count, buf))
		return false;
	auto* cache = static_cast<block_cache*>(dev->cache);
	if (!cache)
		return true;
	auto* src = static_cast<const std::uint8_t*>(buf);
	std::uint32_t block_size = dev->block_size;
	std::uint64_t flags;
	cache->lock.lock_save(flags);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		int idx = cache_find_locked(cache, lba + i);
		if (idx >= 0)
			memory::memcpy(cache->entries[idx].data,
				       src + i * block_size, block_size);
	}
	cache->lock.unlock_restore(flags);
	return true;
}

void device_register(device* dev)
{
	if (!dev)
		return;
	if (!dev->raw_read)
		dev->raw_read = dev->read;
	if (!dev->raw_write)
		dev->raw_write = dev->write;
	if (!dev->cache)
	{
		dev->cache = cache_create(dev->block_size);
		if (!dev->cache)
		{
#ifdef DEBUG
			LOG("cache_disabled; oom_for_%u_byte_blocks",
			    dev->block_size);
#endif
		}
	}

	if (!dev->read)
		dev->read = cached_read;
	if (!dev->write)
		dev->write = cached_write;

	std::uint64_t flags;
	block_lock.lock_save(flags);

	if (!block_devices)
	{
		block_capacity = BLOCK_INIT_CAP;
		block_devices = static_cast<device**>(
		    memory::slub.kcalloc(block_capacity, sizeof(device*)));
	}

	if (block_count >= block_capacity)
	{
		int new_cap = block_capacity * 2;
		auto* new_arr = static_cast<device**>(
		    memory::slub.kcalloc(new_cap, sizeof(device*)));
		if (!new_arr)
		{
			block_lock.unlock_restore(flags);
#ifdef DEBUG
			LOG("oom_registering_device");
#endif
			return;
		}
		for (int i = 0; i < block_count; ++i)
			new_arr[i] = block_devices[i];
		memory::slub.kfree(block_devices);
		block_devices = new_arr;
		block_capacity = new_cap;
	}

	block_devices[block_count++] = dev;
	block_lock.unlock_restore(flags);
#ifdef DEBUG
	LOG("count_blocks_registered=%lu; bytes_per_block=%u",
	    dev->block_count, dev->block_size);
#endif
}

int device_count(void)
{
	return block_count;
}

device* device_get(int index)
{
	if (index < 0 || index >= block_count)
		return nullptr;
	return block_devices[index];
}

void cache_invalidate(device* dev)
{
	if (!dev || !dev->cache)
		return;
	auto* cache = static_cast<block_cache*>(dev->cache);
	std::uint64_t flags;
	cache->lock.lock_save(flags);
	for (int i = 0; i < BLOCK_CACHE_ENTRIES; ++i)
		cache->entries[i].valid = false;
	cache->next_evict = 0;
	cache->lock.unlock_restore(flags);
}

}
