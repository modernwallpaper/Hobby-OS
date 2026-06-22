#include <apic/apic.hpp>
#include <hpet/hpet.hpp>
#include <logging/logger.hpp>
#include <panic/panic.hpp>
#include <tsc/tsc.hpp>

namespace tsc
{

TSC tsc;

void TSC::init(void)
{
	if (!this->verify())
	{
#ifdef DEBUG
		LOG("warning: invariant TSC leaf not set, proceeding anyway");
#endif
	}

	this->calibrate();

#ifdef DEBUG
	LOG("tsc_hz=%llu; tsc_per_us=%llu", this->tsc_hz, this->tsc_per_us);
	LOG("initialized");
#endif
}

bool TSC::verify(void)
{
	std::uint32_t eax, ebx, ecx, edx;

	interrupts::apic::apic.cpuid(0x80000000, eax, ebx, ecx, edx);
	if (eax < 0x80000007)
	{
#ifdef DEBUG
		LOG("extended_leaf_not_present");
#endif
		return false;
	}

	interrupts::apic::apic.cpuid(0x80000007, eax, ebx, ecx, edx);
	if (!(edx & (1 << 8)))
	{
#ifdef DEBUG
		LOG("invariant_tsc_not_present");
#endif
		return false;
	}

	interrupts::apic::apic.cpuid(0x80000001, eax, ebx, ecx, edx);
	if (edx & (1 << 27))
	{
		this->supports_rdtscp = true;
#ifdef DEBUG
		LOG("rdtscp_support=true");
#endif
	}

	return true;
}

void TSC::calibrate(void)
{
	std::uint64_t hpet_hz = timers::hpet::hpet.get_freq();
	std::uint64_t hpet_target = hpet_hz / 1000;

	std::uint64_t flags;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory");

	std::uint64_t hpet_start = timers::hpet::hpet.read_counter();
	std::uint64_t tsc_start =
	    this->supports_rdtscp ? this->rdtscp() : this->rdtsc();

	while (timers::hpet::hpet.read_counter() - hpet_start < hpet_target)
	{
		__asm__ volatile("pause");
	}

	std::uint64_t tsc_end =
	    this->supports_rdtscp ? this->rdtscp() : this->rdtsc();
	std::uint64_t hpet_end = timers::hpet::hpet.read_counter();

	if (flags & 0x200)
		__asm__ volatile("sti");

	std::uint64_t tsc_delta = tsc_end - tsc_start;
	std::uint64_t hpet_delta = hpet_end - hpet_start;

	this->tsc_hz = (tsc_delta * hpet_hz) / hpet_delta;
	this->tsc_per_us = this->tsc_hz / 1000000;
}

std::uint64_t TSC::rdtsc(void)
{
	std::uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

std::uint64_t TSC::rdtscp(void)
{
	std::uint32_t lo, hi;
	__asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
	return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

void TSC::udelay(std::uint64_t us)
{
	std::uint64_t target = this->rdtsc() + us * this->tsc_per_us;
	while (this->rdtsc() < target)
	{
		__asm__ volatile("pause");
	}
}

} // namespace tsc
