#include <apic/ioapic.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/paging.hpp>

namespace interrupts
{

namespace ioapic
{

IOAPIC ioapic;

void IOAPIC::write_register(std::uint8_t index, std::uint32_t value)
{
	*this->regsel =
	    (static_cast<std::uint32_t>(index) & this->REGISTER_INDEX_MASK);
	*this->regwin = value;
}

std::uint32_t IOAPIC::read_register(std::uint8_t index)
{
	*this->regsel =
	    (static_cast<std::uint32_t>(index) & this->REGISTER_INDEX_MASK);
	return *this->regwin;
}

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

int IOAPIC::max_pins(void) const
{
	return this->MAX_REDIRECTION_ENTRIES;
}

void IOAPIC::mask_irq(std::uint8_t irq)
{
	std::uint16_t index = this->REG_IOREDTBL_BASE + irq * 2;
	std::uint32_t low = this->read_register(static_cast<std::uint8_t>(index));
	low |= (1 << 16);
	this->write_register(static_cast<std::uint8_t>(index), low);
}

void IOAPIC::unmask_irq(std::uint8_t irq)
{
	std::uint16_t index = this->REG_IOREDTBL_BASE + irq * 2;
	std::uint32_t low = this->read_register(static_cast<std::uint8_t>(index));
	low &= ~(1 << 16);
	this->write_register(static_cast<std::uint8_t>(index), low);
}

// PCI INTx is level/active-low; legacy ISA is edge/active-high.
void IOAPIC::redirect_irq(std::uint8_t irq, std::uint8_t vector,
			  std::uint8_t apic_id, bool level_triggered,
			  bool active_low)
{
	std::uint16_t index = this->REG_IOREDTBL_BASE + irq * 2;

	std::uint32_t low = vector;

	if (active_low)
		low |= (1 << 13);
	if (level_triggered)
		low |= (1 << 15);

	std::uint32_t high = static_cast<std::uint32_t>(apic_id) << 24;

	this->write_register(static_cast<std::uint8_t>(index), low);
	this->write_register(static_cast<std::uint8_t>(index + 1), high);

#ifdef DEBUG
	LOG("irq=%d; vec_redirect=0x%x; apic=%d; level=%d; active_low=%d",
	    irq, vector, apic_id, level_triggered, active_low);
#endif
}

}

}
