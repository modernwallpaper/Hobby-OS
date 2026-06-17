#include <acpi/acpi.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/paging.hpp>
#include <timers/hpet/hpet.hpp>

namespace timers
{

namespace hpet
{

HPET hpet;

void HPET::init(void)
{
	this->HPET_BASE_ADDRESS = acpi::acpi.hpet_address;
#ifdef DEBUG
	LOG("base_address=%x", this->HPET_BASE_ADDRESS);
#endif

	memory::map_mmio_page(this->HPET_BASE_ADDRESS,
			      this->HPET_BASE_ADDRESS + memory::hhdm_offset);

	this->mmio_base = static_cast<volatile std::uint64_t*>(
	    memory::phys_to_virt(this->HPET_BASE_ADDRESS));

	this->calc_freq();
	this->enable();
#ifdef DEBUG
	LOG("enabled");
#endif
}

void HPET::calc_freq(void)
{
	std::uint64_t cap = this->reg_read(this->GCAP_ID);
	std::uint64_t period = cap >> 32;
	this->frequency = 1000000000000000ULL / period;
}

std::uint64_t HPET::reg_read(std::uint64_t offset)
{
	return this->mmio_base[offset / 8];
}

void HPET::reg_write(std::uint64_t offset, std::uint64_t value)
{
	this->mmio_base[offset / 8] = value;
}

void HPET::enable(void)
{
	std::uint64_t conf = this->reg_read(this->GEN_CONF);
	conf |= this->GEN_CONF_ENABLE;
	this->reg_write(this->GEN_CONF, conf);
}

void HPET::disable(void)
{
	std::uint64_t conf = this->reg_read(this->GEN_CONF);
	conf &= ~this->GEN_CONF_ENABLE;
	this->reg_write(this->GEN_CONF, conf);
}

std::uint64_t HPET::read_counter(void)
{
	return this->reg_read(this->MAIN_CNT);
}

std::uint64_t HPET::get_freq(void)
{
	return this->frequency;
}

} // namespace hpet

} // namespace timers
