#include <acpi/acpi.hpp>
#include <apic/apic.hpp>
#include <apic/ioapic.hpp>
#include <hpet/hpet.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/paging.hpp>
#include <pic/pic.hpp>

namespace interrupts
{

namespace apic
{

APIC apic;

APIC::APIC() : mode(Mode::XAPIC), base(nullptr), bus_frequency(0)
{
}

// read a modelspecific register
std::uint64_t APIC::rdmsr(std::uint32_t msr)
{
	std::uint32_t low, high;
	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
	return (static_cast<std::uint64_t>(high) << 32) | low;
}

// write a modelspecific register
void APIC::wrmsr(std::uint32_t msr, std::uint64_t value)
{
	std::uint32_t low = static_cast<std::uint32_t>(value);
	std::uint32_t high = static_cast<std::uint32_t>(value >> 32);
	__asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

// execute the CPUID instruction
void APIC::cpuid(std::uint32_t leaf, std::uint32_t& eax, std::uint32_t& ebx,
		 std::uint32_t& ecx, std::uint32_t& edx)
{
	__asm__ volatile("cpuid"
			 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			 : "a"(leaf));
}

// xAPIC register write via MMIO
void APIC::xapic_reg_write(std::uint32_t offset, std::uint32_t value)
{
	this->base[offset / 4] = value;
}

// xAPIC register read via MMIO
std::uint32_t APIC::xapic_reg_read(std::uint32_t offset)
{
	return this->base[offset / 4];
}

// x2APIC register write via MSR
void APIC::x2apic_reg_write(std::uint32_t offset, std::uint32_t value)
{
	std::uint32_t msr = 0x800 + (offset >> 4);
	this->wrmsr(msr, value);
}

// x2APIC register read via MSR
std::uint32_t APIC::x2apic_reg_read(std::uint32_t offset)
{
	std::uint32_t msr = 0x800 + (offset >> 4);
	return static_cast<std::uint32_t>(this->rdmsr(msr));
}

// dispatch register write to xAPIC or x2APIC based on current mode
void APIC::reg_write(std::uint32_t offset, std::uint32_t value)
{
	if (this->mode == Mode::X2APIC)
		this->x2apic_reg_write(offset, value);
	else
		this->xapic_reg_write(offset, value);
}

// dispatch register read to xAPIC or x2APIC based on current mode
std::uint32_t APIC::reg_read(std::uint32_t offset)
{
	if (this->mode == Mode::X2APIC)
		return this->x2apic_reg_read(offset);
	else
		return this->xapic_reg_read(offset);
}

// initialize the local APIC: enable, map MMIO, program SVR/TPR/LVT
void APIC::init(std::uint32_t lapic_phys_addr)
{
	std::uint64_t apic_base_msr = this->rdmsr(MSR_APIC_BASE);
#ifdef DEBUG
	LOG("IA32_APIC_BASE=0x%016llx", apic_base_msr);
#endif

	if ((apic_base_msr & (1 << 11)) == 0)
	{
#ifdef DEBUG
		LOG("lapic_disabled_in_msr; enabling");
#endif
		apic_base_msr |= (1 << 11);
		this->wrmsr(MSR_APIC_BASE, apic_base_msr);
	}

	// If the MSR reports a different address than our caller,
	// trust the MSR (the CPU has already been configured by firmware)
	std::uint64_t msr_base = apic_base_msr & 0xFFFFFFFFFFFFF000ULL;
	if (msr_base != lapic_phys_addr)
	{
#ifdef DEBUG
		LOG("lapic_msr_base=0x%llx; differs_from_madt=0x%x; using_msr",
		    msr_base, lapic_phys_addr);
#endif
		lapic_phys_addr = static_cast<std::uint32_t>(msr_base);
	}

	memory::map_mmio_page(lapic_phys_addr,
			      lapic_phys_addr + memory::hhdm_offset);

	this->base = static_cast<volatile std::uint32_t*>(
	    memory::phys_to_virt(lapic_phys_addr));

#ifdef DEBUG
	std::uint32_t version = this->xapic_reg_read(this->REG_VERSION);
	std::uint32_t id = this->xapic_reg_read(this->REG_ID);
	LOG("base=%p; version=0x%x; id=0x%x", this->base, version, id);
#endif

	// Software Enable (SVR), vector 0xFF for spurious
	this->reg_write(REG_SVR, 0xFF | SVR_ENABLE);
#ifdef DEBUG
	LOG("svr=0x%08x", this->reg_read(REG_SVR));
#endif

	// Task Priority; accept all interrupts
	this->reg_write(REG_TPR, 0);

	// Logical Destination: flat model
	this->reg_write(REG_DFR, 0xFFFFFFFF);
	this->reg_write(REG_LDR, 0x01000000);

	// LVT entries
	//   Timer:     masked, oneshot
	this->reg_write(this->REG_LVT_TIMER,
			this->LVT_MASKED | this->LVT_TIMER_ONESHOT);
	//   Thermal:   masked
	this->reg_write(this->REG_LVT_THERMAL, this->LVT_MASKED);
	//   Performanceonitoring counters: masked
	this->reg_write(this->REG_LVT_PMC, this->LVT_MASKED);
	//   LINT0:     masked (legacy IRQ0 from PIC)
	this->reg_write(this->REG_LVT_LINT0, this->LVT_MASKED);
	//   LINT1:     masked (legacy IRQ1 from PIC)
	this->reg_write(this->REG_LVT_LINT1, this->LVT_MASKED);
	//   Error:     vector 0xFE, masked initially
	this->reg_write(this->REG_LVT_ERROR, 0xFE | this->LVT_MASKED);

	// Clear error register
	this->reg_write(this->REG_ESR, 0);
	this->reg_read(this->REG_ESR);

#ifdef DEBUG
	LOG("initialized");
#endif
}

// switch from xAPIC to x2APIC mode (MSRbased access)
bool APIC::enable_x2apic(void)
{
	std::uint32_t eax, ebx, ecx, edx;
	this->cpuid(1, eax, ebx, ecx, edx);

	if (!(ecx & (1 << 21)))
	{
#ifdef DEBUG
		LOG("x2apic_not_supported_by_cpu");
#endif
		return false;
	}

#ifdef DEBUG
	LOG("enabling_x2apic");
#endif

	// Step 1: temporarily disable the LAPIC (clear SVR enable bit)
	this->reg_write(this->REG_SVR,
			this->reg_read(this->REG_SVR) & ~this->SVR_ENABLE);

	// Step 2: set x2APIC enable bit (bit 10) in IA32_APIC_BASE
	std::uint64_t apic_base_msr = this->rdmsr(this->MSR_APIC_BASE);
	apic_base_msr |= (1 << 10); // x2APIC enable
	apic_base_msr |= (1 << 11); // LAPIC enable
	this->wrmsr(MSR_APIC_BASE, apic_base_msr);

	// Step 3: switch to MSR-based access
	this->mode = Mode::X2APIC;

	// Step 4: reenable the LAPIC via MSR
	this->reg_write(this->REG_SVR, 0xFF | this->SVR_ENABLE);

#ifdef DEBUG
	LOG("x2apic_enabled; lapic_id=0x%x", this->get_id());
#endif
	return true;
}

// signal end of interrupt
void APIC::eoi(void)
{
	this->reg_write(this->REG_EOI, 0);
}

// read the local APIC ID
std::uint8_t APIC::get_id(void)
{
	return static_cast<std::uint8_t>(this->reg_read(this->REG_ID) >> 24);
}

// read the local APIC version
std::uint8_t APIC::get_version(void)
{
	return static_cast<std::uint8_t>(this->reg_read(this->REG_VERSION) &
					 0xFF);
}

// check if the APIC is in x2APIC mode
bool APIC::is_x2apic_enabled(void) const
{
	return this->mode == Mode::X2APIC;
}

// send an IPI to a specific APIC ID
void APIC::send_ipi(std::uint8_t apic_id, std::uint8_t vector,
		    std::uint32_t delivery_mode)
{
	if (this->mode == Mode::X2APIC)
	{
		std::uint64_t icr = static_cast<std::uint64_t>(apic_id) << 32;
		icr |= vector | delivery_mode | this->ICR_PHYSICAL |
		       this->ICR_ASSERT;
		this->wrmsr(0x830, icr);
	}
	else
	{
		this->reg_write(this->REG_ICR_HIGH,
				static_cast<std::uint32_t>(apic_id) << 24);
		this->reg_write(this->REG_ICR_LOW, vector | delivery_mode |
						       this->ICR_PHYSICAL |
						       this->ICR_ASSERT);
		while (this->reg_read(this->REG_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");
	}
}

// send an IPI to all other APICs
void APIC::send_ipi_all(std::uint8_t vector, std::uint32_t delivery_mode)
{
	if (this->mode == Mode::X2APIC)
	{
		std::uint64_t icr = vector | delivery_mode | this->ICR_ASSERT |
				    this->ICR_ALL_EXCLUDING_SELF;
		this->wrmsr(0x830, icr);
	}
	else
	{
		this->reg_write(this->REG_ICR_HIGH, 0);
		this->reg_write(this->REG_ICR_LOW,
				vector | delivery_mode |
				    this->ICR_ALL_EXCLUDING_SELF |
				    this->ICR_ASSERT);
		while (this->reg_read(this->REG_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");
	}
}

// send an IPI to self
void APIC::send_ipi_self(std::uint8_t vector, std::uint32_t delivery_mode)
{
	if (this->mode == Mode::X2APIC)
	{
		std::uint64_t icr =
		    vector | delivery_mode | this->ICR_ASSERT | this->ICR_SELF;
		this->wrmsr(0x830, icr);
	}
	else
	{
		this->reg_write(this->REG_ICR_HIGH, 0);
		this->reg_write(this->REG_ICR_LOW, vector | delivery_mode |
						       this->ICR_SELF |
						       this->ICR_ASSERT);
		while (this->reg_read(this->REG_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");
	}
}

// send a startup IPI to bring up an AP
void APIC::send_startup_ipi(std::uint8_t apic_id, std::uint8_t vector)
{
	if (this->mode == Mode::X2APIC)
	{
		std::uint64_t icr = static_cast<std::uint64_t>(apic_id) << 32;
		icr |= vector | this->ICR_STARTUP | this->ICR_PHYSICAL |
		       this->ICR_ASSERT;
		this->wrmsr(0x830, icr);
	}
	else
	{
		this->reg_write(this->REG_ICR_HIGH,
				static_cast<std::uint32_t>(apic_id) << 24);
		this->reg_write(this->REG_ICR_LOW, vector | this->ICR_STARTUP |
						       this->ICR_PHYSICAL |
						       this->ICR_ASSERT);
		while (this->reg_read(this->REG_ICR_LOW) & (1 << 12))
			__asm__ volatile("pause");
	}
}

// calibrate the LAPIC timer bus frequency using the HPET
void APIC::timer_calibrate(void)
{
	std::uint64_t hpet_freq = timers::hpet::hpet.get_freq();
	if (hpet_freq == 0)
		return;

	this->reg_write(this->REG_TIMER_DCR, this->TIMER_DIV_16);

	this->reg_write(this->REG_TIMER_ICR, 0xFFFFFFFF);

	std::uint64_t wait_ticks = hpet_freq / 100;
	std::uint64_t start = timers::hpet::hpet.read_counter();
	while (timers::hpet::hpet.read_counter() - start < wait_ticks)
		__asm__ volatile("pause");

	std::uint32_t remaining = this->reg_read(this->REG_TIMER_CCR);
	std::uint32_t consumed = 0xFFFFFFFF - remaining;

	this->bus_frequency = static_cast<std::uint64_t>(consumed) *
			      this->CALIBRATION_DIVIDER * 100;

	this->reg_write(this->REG_LVT_TIMER,
			this->LVT_MASKED | this->LVT_TIMER_ONESHOT);

#ifdef DEBUG
	LOG("bus_frequency=%llu; consumed=%u", this->bus_frequency, consumed);
#endif
}

// program the LAPIC timer in one-shot mode
void APIC::timer_oneshot(std::uint32_t us, std::uint8_t vector)
{
	if (this->bus_frequency == 0)
		return;

	std::uint32_t count = static_cast<std::uint32_t>(
	    (static_cast<std::uint64_t>(us) *
	     (this->bus_frequency / this->CALIBRATION_DIVIDER)) /
	    1000000);

	this->reg_write(this->REG_TIMER_DCR, this->TIMER_DIV_16);
	this->reg_write(this->REG_TIMER_ICR, count);
	this->reg_write(this->REG_LVT_TIMER, vector | this->LVT_TIMER_ONESHOT);
}

void APIC::timer_periodic(std::uint32_t us, std::uint8_t vector)
{
	if (this->bus_frequency == 0)
		return;

	std::uint32_t count = static_cast<std::uint32_t>(
	    (static_cast<std::uint64_t>(us) *
	     (this->bus_frequency / this->CALIBRATION_DIVIDER)) /
	    1000000);

	this->timer_period_count = count;
	this->timer_period_vector = vector;

	this->reg_write(this->REG_TIMER_DCR, this->TIMER_DIV_16);
	this->reg_write(this->REG_TIMER_ICR, count);
	this->reg_write(this->REG_LVT_TIMER, vector | this->LVT_TIMER_ONESHOT);
}

void APIC::timer_oneshot_periodic_tick(void)
{
	if (this->bus_frequency == 0)
		return;
	this->reg_write(this->REG_TIMER_ICR, this->timer_period_count);
}

// return the calibrated bus frequency
std::uint64_t APIC::bus_freq(void)
{
	return this->bus_frequency;
}

// initialize the full interrupt subsystem: PIC, LAPIC, x2APIC, IOAPIC
void init_all(void)
{
	interrupts::pic::disable_pic();

	std::uint32_t lapic_addr = acpi::acpi.lapic_address;
	if (lapic_addr == 0)
		lapic_addr = 0xFEE00000;

	apic.init(lapic_addr);
	apic.enable_x2apic();

	std::uint32_t ioapic_addr = acpi::acpi.ioapic_address;
	if (ioapic_addr == 0)
		ioapic_addr = 0xFEC00000;

	interrupts::ioapic::ioapic.init(ioapic_addr);

	apic.timer_calibrate();
}

} // namespace apic

} // namespace interrupts
