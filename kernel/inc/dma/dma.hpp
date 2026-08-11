#pragma once

#include <cstddef>
#include <cstdint>

namespace dma
{
struct addr {
	void* virt;
	std::uint64_t phys;
};

addr alloc(std::size_t size);
void free(addr addr, std::size_t size);

// write back (and invalidate) the cache lines covering [addr, addr+size) so
// the DMA engine reads the CPU's latest data. on x86 clflush covers both, but
// this is the operation to run *before* a host->device transfer.
void cache_flush(void* addr, std::size_t size);
// invalidate the cache lines covering [addr, addr+size) so the CPU re-reads
// data written by the device after a device->host transfer completes.
void cache_invalidate(void* addr, std::size_t size);
}
