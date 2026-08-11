#pragma once

#include <cstdint>

namespace interrupts
{

namespace apic
{

class APIC {
private:
	enum class Mode : std::uint8_t {
		XAPIC = 0,
		X2APIC = 1,
	};

	Mode mode;
	volatile std::uint32_t* base;
	std::uint64_t bus_frequency;

	std::uint32_t timer_period_count;
	std::uint8_t timer_period_vector;

	static constexpr std::uint64_t MSR_APIC_BASE = 0x1B;

	static constexpr std::uint32_t REG_ID = 0x020;
	static constexpr std::uint32_t REG_VERSION = 0x030;
	static constexpr std::uint32_t REG_TPR = 0x080;
	static constexpr std::uint32_t REG_EOI = 0x0B0;
	static constexpr std::uint32_t REG_LDR = 0x0D0;
	static constexpr std::uint32_t REG_DFR = 0x0E0;
	static constexpr std::uint32_t REG_SVR = 0x0F0;
	static constexpr std::uint32_t REG_ESR = 0x280;
	static constexpr std::uint32_t REG_ICR_LOW = 0x300;
	static constexpr std::uint32_t REG_ICR_HIGH = 0x310;
	static constexpr std::uint32_t REG_LVT_TIMER = 0x320;
	static constexpr std::uint32_t REG_LVT_THERMAL = 0x330;
	static constexpr std::uint32_t REG_LVT_PMC = 0x340;
	static constexpr std::uint32_t REG_LVT_LINT0 = 0x350;
	static constexpr std::uint32_t REG_LVT_LINT1 = 0x360;
	static constexpr std::uint32_t REG_LVT_ERROR = 0x370;
	static constexpr std::uint32_t REG_TIMER_ICR = 0x380;
	static constexpr std::uint32_t REG_TIMER_CCR = 0x390;
	static constexpr std::uint32_t REG_TIMER_DCR = 0x3E0;

	static constexpr std::uint32_t SVR_ENABLE = 1 << 8;

	static constexpr std::uint32_t LVT_MASKED = 1 << 16;
	static constexpr std::uint32_t LVT_TRIGGER_LEVEL = 1 << 15;
	static constexpr std::uint32_t LVT_REMOTE_IRR = 1 << 14;
	static constexpr std::uint32_t LVT_DELIVERY_FIXED = 0;
	static constexpr std::uint32_t LVT_DELIVERY_SMI = 2;
	static constexpr std::uint32_t LVT_DELIVERY_NMI = 4;
	static constexpr std::uint32_t LVT_DELIVERY_INIT = 5;
	static constexpr std::uint32_t LVT_DELIVERY_EXTINT = 7;
	static constexpr std::uint32_t LVT_DM_NMI = 4;
	static constexpr std::uint32_t LVT_TIMER_ONESHOT = 0;
	static constexpr std::uint32_t LVT_TIMER_PERIODIC = 1 << 17;
	static constexpr std::uint32_t LVT_TIMER_TSC_DEADLINE = 1 << 18;

	static constexpr std::uint32_t TIMER_DIV_1 = 0xB;
	static constexpr std::uint32_t TIMER_DIV_2 = 0x0;
	static constexpr std::uint32_t TIMER_DIV_4 = 0x1;
	static constexpr std::uint32_t TIMER_DIV_8 = 0x2;
	static constexpr std::uint32_t TIMER_DIV_16 = 0x3;
	static constexpr std::uint32_t TIMER_DIV_32 = 0x8;
	static constexpr std::uint32_t TIMER_DIV_64 = 0x9;
	static constexpr std::uint32_t TIMER_DIV_128 = 0xA;

	static constexpr std::uint32_t CALIBRATION_DIVIDER = 16;

	static constexpr std::uint32_t ICR_FIXED = 0;
	static constexpr std::uint32_t ICR_LOWEST_PRIORITY = 1;
	static constexpr std::uint32_t ICR_SMI = 2;
	static constexpr std::uint32_t ICR_NMI = 4;
	static constexpr std::uint32_t ICR_INIT = 5;
	static constexpr std::uint32_t ICR_STARTUP = 6;

	static constexpr std::uint32_t ICR_PHYSICAL = 0;
	static constexpr std::uint32_t ICR_LOGICAL = 1 << 11;

	static constexpr std::uint32_t ICR_ASSERT = 1 << 14;
	static constexpr std::uint32_t ICR_DEASSERT = 0;
	static constexpr std::uint32_t ICR_EDGE = 0;
	static constexpr std::uint32_t ICR_LEVEL = 1 << 15;

	static constexpr std::uint32_t ICR_NO_SHORTHAND = 0;
	static constexpr std::uint32_t ICR_SELF = 1 << 18;
	static constexpr std::uint32_t ICR_ALL_INCLUDING_SELF = 2 << 18;
	static constexpr std::uint32_t ICR_ALL_EXCLUDING_SELF = 3 << 18;

	static std::uint64_t rdmsr(std::uint32_t msr);
	static void wrmsr(std::uint32_t msr, std::uint64_t value);

	void xapic_reg_write(std::uint32_t offset, std::uint32_t value);
	std::uint32_t xapic_reg_read(std::uint32_t offset);

	void x2apic_reg_write(std::uint32_t offset, std::uint32_t value);
	std::uint32_t x2apic_reg_read(std::uint32_t offset);

	void reg_write(std::uint32_t offset, std::uint32_t value);
	std::uint32_t reg_read(std::uint32_t offset);

public:
	APIC();

	void init(std::uint32_t lapic_phys_addr);

	static void cpuid(std::uint32_t leaf, std::uint32_t& eax,
			  std::uint32_t& ebx, std::uint32_t& ecx,
			  std::uint32_t& edx);

	bool enable_x2apic(void);
	void eoi(void);
	std::uint8_t get_id(void);
	std::uint8_t get_version(void);
	bool is_x2apic_enabled(void) const;

	void send_ipi(std::uint8_t apic_id, std::uint8_t vector,
		      std::uint32_t delivery_mode);
	void send_ipi_all(std::uint8_t vector, std::uint32_t delivery_mode);
	void send_ipi_self(std::uint8_t vector, std::uint32_t delivery_mode);
	void send_startup_ipi(std::uint8_t apic_id, std::uint8_t vector);

	void timer_calibrate(void);
	void timer_oneshot(std::uint32_t us, std::uint8_t vector);
	void timer_periodic(std::uint32_t us, std::uint8_t vector);
	void timer_oneshot_periodic_tick(void);
	std::uint64_t bus_freq(void);
};

extern APIC apic;

void init_all(void);

}

}
