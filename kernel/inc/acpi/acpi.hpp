#pragma once

#include <cstdint>

namespace acpi
{

struct __attribute__((packed)) rsdp {
	char signature[8];
	std::uint8_t checksum;
	char oem_id[6];
	std::uint8_t revision;
	std::uint32_t rsdt_address;
	std::uint32_t length;
	std::uint64_t xsdt_address;
	std::uint8_t extended_checksum;
	std::uint8_t reserved[3];
};

struct __attribute__((packed)) sdt_header {
	char signature[4];
	std::uint32_t length;
	std::uint8_t revision;
	std::uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	std::uint32_t oem_revision;
	std::uint32_t creator_id;
	std::uint32_t creator_revision;
};

struct __attribute__((packed)) madt {
	sdt_header header;
	std::uint32_t local_apic_address;
	std::uint32_t flags;
};

struct __attribute__((packed)) madt_entry_header {
	std::uint8_t type;
	std::uint8_t length;
};

enum madt_entry_type : std::uint8_t {
	MADT_LOCAL_APIC = 0,
	MADT_IO_APIC = 1,
	MADT_INT_OVERRIDE = 2,
	MADT_NMI_SOURCE = 3,
	MADT_LOCAL_APIC_NMI = 4,
	MADT_LAPIC_OVERRIDE = 5,
	MADT_LOCAL_X2APIC = 9,
	MADT_LOCAL_X2APIC_NMI = 10,
};

struct __attribute__((packed)) madt_entry_local_apic {
	madt_entry_header header;
	std::uint8_t processor_id;
	std::uint8_t apic_id;
	std::uint32_t flags;
};

struct __attribute__((packed)) madt_entry_io_apic {
	madt_entry_header header;
	std::uint8_t ioapic_id;
	std::uint8_t reserved;
	std::uint32_t ioapic_address;
	std::uint32_t gsi_base;
};

struct __attribute__((packed)) madt_entry_lapic_override {
	madt_entry_header header;
	std::uint16_t reserved;
	std::uint64_t lapic_address;
};

struct __attribute__((packed)) madt_entry_x2apic {
	madt_entry_header header;
	std::uint16_t reserved;
	std::uint32_t x2apic_id;
	std::uint32_t flags;
	std::uint32_t processor_uid;
};

struct __attribute__((packed)) madt_entry_int_override {
	madt_entry_header header;
	std::uint8_t bus;
	std::uint8_t source;
	std::uint32_t irq;
	std::uint16_t flags;
};

static constexpr int MAX_CPU = 16;

struct CpuInfo {
	std::uint8_t apic_id;
	bool bsp;
};

class ACPI {
private:
	rsdp* rsdp_ptr;
	madt* madt_table;

	static constexpr int MAX_ISO_OVERRIDES = 16;

	struct IsoOverride {
		std::uint8_t source;
		std::uint32_t irq;
		std::uint16_t flags;
	};

	IsoOverride iso_overrides[MAX_ISO_OVERRIDES];
	int iso_override_count;

	bool validate_checksum(void* table, std::uint32_t length);
	sdt_header* find_table(const char* signature);

	void parse_cpu_entries(void);

public:
	std::uint32_t lapic_address;
	std::uint32_t ioapic_address;
	std::uint8_t ioapic_id;
	std::uint32_t ioapic_gsi_base;
	std::uint64_t hpet_address;
	bool x2apic_present;

	CpuInfo cpus[MAX_CPU];
	int cpu_count;

	void init(void* rsdp_addr);
	std::uint32_t resolve_irq(std::uint8_t irq);
};

extern ACPI acpi;

}
