#pragma once

#include <cstdint>

namespace memory
{

void map_mmio_page(std::uint64_t phys_addr, std::uint64_t virt_addr);
void map_mmio_range(std::uint64_t phys_addr, std::uint64_t virt_addr,
		    std::uint64_t size);

} // namespace memory
