#include <acpi/acpi.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <timers/hpet/hpet.hpp>

namespace acpi
{

ACPI acpi;

// verify an ACPI table checksum (all bytes sum to 0)
bool ACPI::validate_checksum(void* table, std::uint32_t length)
{
	std::uint8_t sum = 0;
	auto* bytes = static_cast<std::uint8_t*>(table);
	for (std::uint32_t i = 0; i < length; i++)
		sum += bytes[i];
	return sum == 0;
}

// search the RSDT (or XSDT) for a table with the given 4-byte signature
sdt_header* ACPI::find_table(const char* signature)
{
	if (this->rsdp_ptr->revision >= 2 && this->rsdp_ptr->xsdt_address != 0)
	{
		auto* xsdt = static_cast<sdt_header*>(
		    memory::phys_to_virt(this->rsdp_ptr->xsdt_address));
		std::uint32_t count = (xsdt->length - sizeof(sdt_header)) / 8;
		auto* entries = reinterpret_cast<std::uint64_t*>(
		    reinterpret_cast<std::uintptr_t>(xsdt) +
		    sizeof(sdt_header));

		for (std::uint32_t i = 0; i < count; i++)
		{
			auto* table = static_cast<sdt_header*>(
			    memory::phys_to_virt(entries[i]));
			if (memory::memcmp(table->signature, signature, 4) == 0)
				return table;
		}
	}
	else
	{
		auto* rsdt = static_cast<sdt_header*>(
		    memory::phys_to_virt(this->rsdp_ptr->rsdt_address));
		std::uint32_t count = (rsdt->length - sizeof(sdt_header)) / 4;
		auto* entries = reinterpret_cast<std::uint32_t*>(
		    reinterpret_cast<std::uintptr_t>(rsdt) +
		    sizeof(sdt_header));

		for (std::uint32_t i = 0; i < count; i++)
		{
			auto* table = static_cast<sdt_header*>(
			    memory::phys_to_virt(entries[i]));
			if (memory::memcmp(table->signature, signature, 4) == 0)
				return table;
		}
	}
	return nullptr;
}

// parse the RSDP and MADT, extracting LAPIC address, IOAPIC info, and x2APIC
// presence
void ACPI::init(void* rsdp_addr)
{
	this->rsdp_ptr = static_cast<rsdp*>(rsdp_addr);

	std::uint32_t rsdp_len =
	    (this->rsdp_ptr->revision >= 2) ? this->rsdp_ptr->length : 20;
	if (!this->validate_checksum(this->rsdp_ptr, rsdp_len))
	{
#ifdef DEBUG
		LOG("rsdp_checksum_failed");
#endif
		return;
	}

#ifdef DEBUG
	LOG("revision=%d; oem=%.6s", this->rsdp_ptr->revision,
	    this->rsdp_ptr->oem_id);
#endif

	this->madt_table = reinterpret_cast<madt*>(this->find_table("APIC"));
	if (!this->madt_table)
	{
#ifdef DEBUG
		LOG("madt_not_found");
#endif
		return;
	}

	if (!this->validate_checksum(this->madt_table,
				     this->madt_table->header.length))
	{
#ifdef DEBUG
		LOG("madt_checksum_failed");
#endif
		return;
	}

	auto* hpet_table = reinterpret_cast<struct timers::hpet::hpet*>(
	    this->find_table("HPET"));
	this->hpet_address = hpet_table->address.address;

	this->lapic_address = this->madt_table->local_apic_address;
	this->ioapic_address = 0;
	this->x2apic_present = false;

	std::uintptr_t entry_ptr =
	    reinterpret_cast<std::uintptr_t>(this->madt_table) + sizeof(madt);
	std::uintptr_t end =
	    reinterpret_cast<std::uintptr_t>(this->madt_table) +
	    this->madt_table->header.length;

	while (entry_ptr < end)
	{
		auto* entry = reinterpret_cast<madt_entry_header*>(entry_ptr);

		switch (entry->type)
		{
		case MADT_IO_APIC: {
			auto* io = reinterpret_cast<madt_entry_io_apic*>(entry);
			this->ioapic_address = io->ioapic_address;
			this->ioapic_id = io->ioapic_id;
			this->ioapic_gsi_base = io->gsi_base;
#ifdef DEBUG
			LOG("io_apic_id=%d; addr=0x%x; gsi=%u", this->ioapic_id,
			    this->ioapic_address, this->ioapic_gsi_base);
#endif
			break;
		}
		case MADT_LAPIC_OVERRIDE: {
			auto* ov =
			    reinterpret_cast<madt_entry_lapic_override*>(entry);
			this->lapic_address =
			    static_cast<std::uint32_t>(ov->lapic_address);
#ifdef DEBUG
			LOG("lapic_address_override=0x%x", this->lapic_address);
#endif
			break;
		}
		case MADT_LOCAL_X2APIC:
			this->x2apic_present = true;
			break;
		}

		entry_ptr += entry->length;
	}

	if (this->ioapic_address == 0)
		this->ioapic_address = 0xFEC00000;

#ifdef DEBUG
	LOG("lapic=0x%x; ioapic=0x%x; x2apic=%s", this->lapic_address,
	    this->ioapic_address, this->x2apic_present ? "yes" : "no");
#endif
}

} // namespace acpi
