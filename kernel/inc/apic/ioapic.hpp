#pragma once

#include <cstdint>

namespace interrupts
{

namespace ioapic
{

class IOAPIC {
private:
	volatile std::uint32_t* regsel;
	volatile std::uint32_t* regwin;
	std::uint64_t base_phys;

	static constexpr std::uint64_t IOAPIC_DEFAULT_BASE = 0xFEC00000;
	static constexpr std::uint32_t REGISTER_INDEX_MASK = 0xFF;

	static constexpr int MAX_REDIRECTION_ENTRIES = 24;

	static constexpr std::uint8_t REG_IOAPICID = 0x00;
	static constexpr std::uint8_t REG_IOAPICVER = 0x01;
	static constexpr std::uint8_t REG_IOAPICARB = 0x02;
	static constexpr std::uint8_t REG_IOREDTBL_BASE = 0x10;

	void write_register(std::uint8_t index, std::uint32_t value);
	std::uint32_t read_register(std::uint8_t index);

public:
	void init(std::uint32_t base_phys = IOAPIC_DEFAULT_BASE);
	int max_pins(void) const;
	void mask_irq(std::uint8_t irq);
	void unmask_irq(std::uint8_t irq);
	void redirect_irq(std::uint8_t irq, std::uint8_t vector,
			  std::uint8_t apic_id, bool level_triggered = false,
			  bool active_low = false);
};

extern IOAPIC ioapic;

} // namespace ioapic

} // namespace interrupts
