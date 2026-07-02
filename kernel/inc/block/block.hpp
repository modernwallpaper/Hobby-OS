#pragma once

#include <cstdint>

namespace block
{
struct device {
	bool (*read)(device* dev, std::uint64_t lba, std::uint32_t count,
		     void* buf);
	bool (*write)(device* dev, std::uint64_t lba, std::uint32_t count,
		      const void* buf);
	bool (*raw_read)(device* dev, std::uint64_t lba, std::uint32_t count,
			 void* buf);
	bool (*raw_write)(device* dev, std::uint64_t lba, std::uint32_t count,
			  const void* buf);
	std::uint64_t block_count;
	std::uint32_t block_size;
	void* priv;
	void* cache;
};

void device_register(device* dev);
int device_count(void);
device* device_get(int index);
void cache_invalidate(device* dev);
} // namespace block
