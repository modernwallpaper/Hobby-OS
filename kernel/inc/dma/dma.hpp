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
} // namespace dma
