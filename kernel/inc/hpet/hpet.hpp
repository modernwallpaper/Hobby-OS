#pragma once

#include <cstdint>

namespace timers
{

namespace hpet
{

struct address_structure {
	std::uint8_t address_space_id;
	std::uint8_t register_bit_width;
	std::uint8_t register_bit_offset;
	std::uint8_t reserved;
	std::uint64_t address;
} __attribute__((packed));

struct description_table_header {
	char signature[4];
	std::uint32_t lenght;
	std::uint8_t revision;
	std::uint8_t checksum;
	char oemid[6];
	std::uint64_t oem_table_id;
	std::uint32_t oem_revision;
	std::uint32_t creator_id;
	std::uint32_t creater_revision;
} __attribute__((packed));

struct hpet {
	description_table_header header;
	std::uint8_t hardware_rev_id;
	std::uint8_t comparator_count : 5;
	std::uint8_t counter_size : 1;
	std::uint8_t reserved : 1;
	std::uint8_t legacy_replacement : 1;
	std::uint16_t pci_vendor_id;
	address_structure address;
	std::uint8_t hpet_number;
	std::uint16_t minimum_tick;
	std::uint8_t page_protection;
} __attribute__((packed));

class HPET {
private:
	static constexpr std::uint64_t GCAP_ID = 0x000;
	static constexpr std::uint64_t GEN_CONF = 0x010;
	static constexpr std::uint64_t GEN_STS = 0x020;
	static constexpr std::uint64_t MAIN_CNT = 0x0F0;
	static constexpr std::uint64_t T0_CONF = 0x100;
	static constexpr std::uint64_t T0_COMP = 0x108;
	static constexpr std::uint64_t GEN_CONF_ENABLE = 1ULL << 0;

	std::uint64_t HPET_BASE_ADDRESS;
	volatile std::uint64_t* mmio_base;
	std::uint64_t frequency;

	void calc_freq(void);
	std::uint64_t reg_read(std::uint64_t offset);
	void reg_write(std::uint64_t offset, std::uint64_t value);

public:
	void init(void);
	void enable(void);
	void disable(void);
	std::uint64_t read_counter(void);
	std::uint64_t get_freq(void);
};

extern HPET hpet;

}

}
