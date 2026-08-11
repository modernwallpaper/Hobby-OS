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
}

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

	// `next` is for run/reaper queues; `wait_next` is for one wait queue.
	std::uint64_t wake_tick;
	thread* wait_next;
	void (*entry)(void);
	bool stack_owned;
};

// Per-CPU run queue; no global scheduler lock is held across switches.
class Runqueue {
private:
	std::uint64_t cpu;
	thread* idle_thread;
	round_robin::RoundRobin rr_sched;
	deadline::Deadline deadline_sched;
	realtime::Realtime realtime_sched;

public:
	void init(std::uint64_t cpu);
	void set_idle(thread* t);
	void enqueue(thread* t);
	thread* pick_next(void);
	thread* get_idle(void);
};

// Per-CPU sleep queue, sorted by wake tick.
class SleepQueue {
private:
	sync::IrqSpinlock lock;
	thread* head;

public:
	void init(void);
	void insert(thread* t);
	bool remove(thread* t);
	void wake_expired(std::uint64_t now, Runqueue& rq);
};

class Scheduler {
private:
	idle::Idle idle_sched;
	Runqueue runqueues[smp::MAX_CPUS];
	SleepQueue sleep_queues[smp::MAX_CPUS];

	// Freed only after the scheduler has switched away from their stacks.
	sync::IrqSpinlock graveyard_lock;
	thread* graveyard;

	interrupts::idt::frame* schedule(interrupts::idt::frame* f,
					bool preempt);
	void graveyard_push(thread* t);
	void reap_graveyard(void);

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
	void sleep(std::uint64_t ticks);
	void wake(thread* t);
	[[noreturn]] void exit(void);
};

extern Scheduler scheduler;

// Coarse scheduler time base; per-CPU LAPIC timers are not phase-locked.
extern std::uint64_t ticks_since_boot;

static inline thread* current_thread(void)
{
	return reinterpret_cast<thread*>(smp::this_cpu()->current_thread);
}

}
