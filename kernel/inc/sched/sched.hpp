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

	// sleep / wait-queue support. `next` links a thread into a run queue (or
	// the reaper's graveyard); `wait_next` links a thread into exactly one of
	// the sleep queue, a mutex wait queue, or a condvar wait queue. A thread
	// is never a member of more than one wait queue at a time.
	std::uint64_t wake_tick;
	thread* wait_next;
	// entry point invoked by the thread trampoline after a context switch
	void (*entry)(void);
	// whether kernel_stack_base was handed out by create_thread() and must be
	// returned to the buddy allocator when the thread is reaped
	bool stack_owned;
};

// Per-CPU run queue: one set of policy sub-queues per CPU, plus a pointer to
// that CPU's idle thread. The scheduling fast path only ever touches the
// current CPU's Runqueue, so it never contends on a global scheduler lock.
// Each sub-queue keeps its own IrqSpinlock; every queue operation is short
// and self-contained, so no lock is ever held across a scheduling decision or
// across the actual context switch.
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

// Per-CPU sleep queue: threads that called sleep() park here, sorted by wake
// tick. Only the owning CPU's timer tick walks it, so expired threads are
// re-enqueued onto that same CPU's run queue. wake() from another CPU takes
// this queue's lock to unlink a thread before enqueueing it, so it never
// races the tick path.
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

	// dead threads awaiting reclamation; drained at the top of schedule().
	// Pushed only after the scheduler has switched away from a dead thread,
	// so the stack is no longer in use by the time it is freed.
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

// Monotonic tick count, incremented by every CPU's LAPIC timer tick. The
// per-CPU timers are not phase-locked, so this is a coarse time base used
// only for scheduling delays.
extern std::uint64_t ticks_since_boot;

static inline thread* current_thread(void)
{
	return reinterpret_cast<thread*>(smp::this_cpu()->current_thread);
}

} // namespace sched
