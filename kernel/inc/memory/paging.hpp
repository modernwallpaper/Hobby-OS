#pragma once

#include <cstdint>

namespace memory
{

void map_mmio_page(std::uint64_t phys_addr, std::uint64_t virt_addr);

} // namespace memory
