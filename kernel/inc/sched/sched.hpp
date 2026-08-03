#pragma once

#include <cstdint>
#include <smp/smp.hpp>
#include <sync/spinlock.hpp>

#include "deadline.hpp"
#include "idle.hpp"
#include "round_robin.hpp"
#include "realtime.hpp"

namespace interrupts::idt
{
struct frame;
} // namespace interrupts::idt

namespace sched
{

static constexpr std::uint64_t DEFAULT_TIMESLICE = 5;

enum class Policy : std::uint8_t {
	NORMAL,
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
	std::uint64_t remaining_ticks;
	std::uint64_t cpu;
	std::uint64_t deadline;
};

class Scheduler {
private:
	deadline::Deadline deadline_sched;
	round_robin::RoundRobin rr_sched;
	idle::Idle idle_sched;
	realtime::Realtime realtime_sched;

	sync::IrqSpinlock lock;

	thread* get_idle(void);
	interrupts::idt::frame* schedule(interrupts::idt::frame* f,
					bool preempt);

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
	interrupts::idt::frame* tick(interrupts::idt::frame* f);
	interrupts::idt::frame* yield(interrupts::idt::frame* f);
	static void yield(void);
	thread* create_thread(void (*entry)(void), Policy policy,
			      std::uint64_t cpu = 0,
			      std::uint64_t deadline = 0);
};

extern Scheduler scheduler;

static inline thread* current_thread(void)
{
	return reinterpret_cast<thread*>(smp::this_cpu()->current_thread);
}

} // namespace sched
