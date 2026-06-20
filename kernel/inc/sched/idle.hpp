#pragma once

#include <cstdint>
#include <smp/smp.hpp>

namespace sched
{

struct thread;

namespace idle
{

class Idle {
private:
	thread* idle_threads[smp::MAX_CPUS];

public:
	void init(void);
	thread* get_idle(std::uint64_t cpu);
};

} // namespace idle

} // namespace sched
