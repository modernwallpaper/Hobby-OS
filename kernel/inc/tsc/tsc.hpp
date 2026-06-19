#pragma once

#include <cstdint>

namespace tsc
{

class TSC {
private:
	bool supports_rdtscp = false;
	std::uint64_t tsc_hz;
	std::uint64_t tsc_per_us;

public:
	void init(void);
	bool verify(void);
	void calibrate(void);
	std::uint64_t rdtsc(void);
	std::uint64_t rdtscp(void);
	void udelay(std::uint64_t us);
};

extern TSC tsc;

} // namespace tsc
