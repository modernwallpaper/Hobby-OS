#include <apic/ioapic.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/paging.hpp>

namespace interrupts
{

namespace ioapic
{

IOAPIC ioapic;

// write an IOAPIC register (indexed via regsel/regwin)
void IOAPIC::write_register(std::uint8_t index, std::uint32_t value)
{
	*this->regsel =
	    (static_cast<std::uint32_t>(index) & this->REGISTER_INDEX_MASK);
	*this->regwin = value;
}

// read an IOAPIC register (indexed via regsel/regwin)
std::uint32_t IOAPIC::read_register(std::uint8_t index)
{
	*this->regsel =
	    (static_cast<std::uint32_t>(index) & this->REGISTER_INDEX_MASK);
	return *this->regwin;
}

// initialize the IOAPIC: map MMIO, mask all redirection entries
void IOAPIC::init(std::uint32_t base_phys)
{
	this->base_phys = base_phys;

	memory::map_mmio_page(this->base_phys,
			      this->base_phys + memory::hhdm_offset);

	void* virt_base = memory::phys_to_virt(this->base_phys);

	this->regsel = static_cast<volatile std::uint32_t*>(virt_base);
	this->regwin = reinterpret_cast<volatile std::uint32_t*>(
	    reinterpret_cast<std::uintptr_t>(virt_base) + 0x10);

	std::uint32_t version = this->read_register(this->REG_IOAPICVER);

	int max_entries = (version >> 16) & 0xFF;
#ifdef DEBUG
	std::uint32_t ioapic_id = this->read_register(this->REG_IOAPICID);
	LOG("id=0x%08x; version=0x%08x; max_entries=%d;", ioapic_id, version,
	    max_entries);
#endif

	if (max_entries > this->MAX_REDIRECTION_ENTRIES - 1)
		max_entries = this->MAX_REDIRECTION_ENTRIES - 1;

	for (int i = 0; i <= max_entries; i++)
		this->mask_irq(static_cast<std::uint8_t>(i));

#ifdef DEBUG
	LOG("entries_masked=%d", max_entries + 1);
	LOG("initialzed");
#endif
}

// mask (disable) a specific IRQ pin
void IOAPIC::mask_irq(std::uint8_t irq)
{
	std::uint8_t index =
	    static_cast<std::uint8_t>(this->REG_IOREDTBL_BASE + irq * 2);
	std::uint32_t low = this->read_register(index);
	low |= (1 << 16);
	this->write_register(index, low);
}

// unmask (enable) a specific IRQ pin
void IOAPIC::unmask_irq(std::uint8_t irq)
{
	std::uint8_t index =
	    static_cast<std::uint8_t>(this->REG_IOREDTBL_BASE + irq * 2);
	std::uint32_t low = this->read_register(index);
	low &= ~(1 << 16);
	this->write_register(index, low);
}

// route an IRQ pin to a specific APIC and interrupt vector
void IOAPIC::redirect_irq(std::uint8_t irq, std::uint8_t vector,
			  std::uint8_t apic_id)
{
	std::uint8_t index =
	    static_cast<std::uint8_t>(this->REG_IOREDTBL_BASE + irq * 2);

	std::uint32_t low = vector;

	std::uint32_t high = static_cast<std::uint32_t>(apic_id) << 24;

	this->write_register(index, low);
	this->write_register(static_cast<std::uint8_t>(index + 1), high);

#ifdef DEBUG
	LOG("irq=%d; vec_redirect=0x%x; apic=%d", irq, vector, apic_id);
#endif
}

} // namespace ioapic

} // namespace interrupts
