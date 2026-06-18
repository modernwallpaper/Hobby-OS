#pragma once

#include <acpi/acpi.hpp>
#include <cstdint>

namespace smp
{

static constexpr std::uint64_t TRAMPOLINE_PAGE = 0x7000;

extern "C" void ap_entry(std::uint32_t cpu_id);

void wake_aps(void);

} // namespace smp
