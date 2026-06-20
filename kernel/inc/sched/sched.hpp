#pragma once

#include <cstdint>
#include <smp/smp.hpp>

#include "deadline.hpp"
#include "eevdf.hpp"
#include "idle.hpp"
#include "realtime.hpp"

namespace sched
{

enum class Policy : std::uint8_t {
	EEVDF,
	DEADLINE,
	REALTIME,
	IDLE
};

enum class TaskState : std::uint8_t {
	READY,
	RUNNING,
	SLEEPING,
	DEAD
};

struct thread {
	TaskState state;
	Policy policy;
	std::uint64_t* rsp;
	void* kernel_stack_base;
	thread* next;
};

class Scheduler {
private:
	deadline::Deadline deadline_sched;
	eevdf::Eevdf eevdf_sched;
	idle::Idle idle_sched;
	realtime::Realtime realtime_sched;

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
	void tick(void);
	void yield(void);
};

extern Scheduler scheduler;

static inline thread* current_thread(void)
{
	return reinterpret_cast<thread*>(smp::this_cpu()->current_thread);
}

} // namespace sched
