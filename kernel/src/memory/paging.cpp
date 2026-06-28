#include <memory/buddy.hpp>
#include <memory/paging.hpp>
#include <logging/logger.hpp>
#include <panic/panic.hpp>

namespace memory
{

static constexpr std::uint64_t PAGE_PRESENT_RW = 0x03;
static constexpr std::uint64_t PAGE_MMIO_FLAGS = 0x13;

// extract the page-table index at a given level (PML4=3, PDPT=2, PD=1, PT=0)
static int pt_index(std::uint64_t vaddr, int level)
{
	return static_cast<int>((vaddr >> (12 + level * 9)) & 0x1FF);
}

// descend one level, allocating a new page table if missing
static std::uint64_t* walk_level(std::uint64_t* table, int idx)
{
	if ((table[idx] & 1) == 0)
	{
		std::uint64_t new_page = buddy.alloc_pages(0);
		if (!new_page)
			PANIC("map_mmio_page: failed to allocate page table");
		std::uint64_t* new_table =
		    static_cast<std::uint64_t*>(phys_to_virt(new_page));
		for (int i = 0; i < 512; i++)
			new_table[i] = 0;
		table[idx] = new_page | PAGE_PRESENT_RW;
	}
	return static_cast<std::uint64_t*>(
	    phys_to_virt(table[idx] & ~0xFFFULL));
}

// map a single 4K page for MMIO with uncacheable+write-through flags
void map_mmio_page(std::uint64_t phys_addr, std::uint64_t virt_addr)
{
	std::uint64_t phys_page = phys_addr & ~0xFFFULL;
	std::uint64_t virt_page = virt_addr & ~0xFFFULL;

	std::uint64_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

	auto* pml4 =
	    static_cast<std::uint64_t*>(phys_to_virt(cr3 & ~0xFFFULL));

	std::uint64_t* pdpt = walk_level(pml4, pt_index(virt_page, 3));
	std::uint64_t* pd = walk_level(pdpt, pt_index(virt_page, 2));
	std::uint64_t* pt = walk_level(pd, pt_index(virt_page, 1));

	pt[pt_index(virt_page, 0)] = phys_page | PAGE_MMIO_FLAGS;

	__asm__ volatile("invlpg %0" : : "m"(*(volatile char*)virt_page));
}

} // namespace memory
