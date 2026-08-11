#include <cstdint>
#include <idt/idt.hpp>
#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/slub.hpp>
#include <panic/panic.hpp>
#include <sched/sched.hpp>

extern "C" void idle_loop(void);

namespace sched
{

std::uint64_t ticks_since_boot = 0;

Scheduler scheduler;

namespace
{

// Entry point for new threads; returning marks the thread dead for reaping.
extern "C" void thread_entry_trampoline(void)
{
	thread* t = current_thread();
	if (t && t->entry)
		t->entry();
	scheduler.exit();
}

}

void Runqueue::init(std::uint64_t cpu)
{
	this->cpu = cpu;
	this->idle_thread = nullptr;
	this->rr_sched.init();
	this->deadline_sched.init();
	this->realtime_sched.init();
}

void Runqueue::set_idle(thread* t)
{
	this->idle_thread = t;
}

void Runqueue::enqueue(thread* t)
{
	t->state = TaskState::READY;

	switch (t->policy)
	{
	case Policy::REALTIME:
		this->realtime_sched.enqueue(t);
		break;
	case Policy::DEADLINE:
		this->deadline_sched.enqueue(t);
		break;
	case Policy::NORMAL:
		this->rr_sched.enqueue(t);
		break;
	case Policy::IDLE:
		break;
	}
}

thread* Runqueue::pick_next(void)
{
	thread* t;

	t = this->realtime_sched.pick_next();
	if (t)
		return t;

	t = this->deadline_sched.pick_next();
	if (t)
		return t;

	t = this->rr_sched.pick_next();
	if (t)
		return t;

	return this->get_idle();
}

thread* Runqueue::get_idle(void)
{
	return this->idle_thread;
}

void SleepQueue::init(void)
{
	this->head = nullptr;
}

void SleepQueue::insert(thread* t)
{
	std::uint64_t flags;
	this->lock.lock_save(flags);

	t->state = TaskState::SLEEPING;
	t->wait_next = nullptr;

	if (!this->head || t->wake_tick < this->head->wake_tick)
	{
		t->wait_next = this->head;
		this->head = t;
		this->lock.unlock_restore(flags);
		return;
	}

	thread* prev = this->head;
	while (prev->wait_next &&
	       prev->wait_next->wake_tick <= t->wake_tick)
		prev = prev->wait_next;

	t->wait_next = prev->wait_next;
	prev->wait_next = t;

	this->lock.unlock_restore(flags);
}

bool SleepQueue::remove(thread* t)
{
	std::uint64_t flags;
	this->lock.lock_save(flags);

	thread* prev = nullptr;
	for (thread* cur = this->head; cur; prev = cur, cur = cur->wait_next)
	{
		if (cur == t)
		{
			if (prev)
				prev->wait_next = cur->wait_next;
			else
				this->head = cur->wait_next;
			cur->wait_next = nullptr;
			this->lock.unlock_restore(flags);
			return true;
		}
	}

	this->lock.unlock_restore(flags);
	return false;
}

void SleepQueue::wake_expired(std::uint64_t now, Runqueue& rq)
{
	std::uint64_t flags;
	this->lock.lock_save(flags);

	// Expired sleepers are at the head; lock order is sleep queue -> run queue.
	thread* w = this->head;
	while (w && w->wake_tick <= now)
	{
		this->head = w->wait_next;
		w->wait_next = nullptr;
		rq.enqueue(w);
		w = this->head;
	}

	this->lock.unlock_restore(flags);
}

void Scheduler::init(void)
{
	this->idle_sched.init();

	for (std::uint64_t i = 0; i < smp::MAX_CPUS; i++)
	{
		this->runqueues[i].init(i);
		this->runqueues[i].set_idle(this->idle_sched.get_idle(i));
		this->sleep_queues[i].init();
	}
	this->graveyard = nullptr;

	smp::cpu_info* bsp = smp::this_cpu();

	thread* initial =
	    static_cast<thread*>(memory::slub.kmalloc(sizeof(thread)));
	if (!initial)
		PANIC("sched_init: kmalloc failed");

	initial->state = TaskState::RUNNING;
	initial->policy = Policy::NORMAL;
	initial->rsp = nullptr;
	initial->kernel_stack_base = nullptr;
	initial->next = nullptr;
	initial->remaining_ticks = DEFAULT_TIMESLICE;
	initial->cpu = bsp->cpu_id;
	initial->deadline = 0;
	initial->wake_tick = 0;
	initial->wait_next = nullptr;
	initial->entry = nullptr;
	initial->stack_owned = false;

	bsp->current_thread = initial;

	for (std::uint64_t i = 0; i < smp::MAX_CPUS; i++)
	{
		smp::cpu_info* cpu = &smp::cpu_infos[i];
		if (cpu == bsp)
			continue;

		thread* idle = this->runqueues[i].get_idle();
		if (idle)
		{
			idle->state = TaskState::RUNNING;
			cpu->current_thread = idle;
		}
	}
}

void Scheduler::enqueue(thread* t)
{
	if (!t || t->cpu >= smp::MAX_CPUS)
		return;
	this->runqueues[t->cpu].enqueue(t);
}

thread* Scheduler::pick_next(void)
{
	return this->runqueues[smp::this_cpu()->cpu_id].pick_next();
}

void Scheduler::graveyard_push(thread* t)
{
	std::uint64_t flags;
	this->graveyard_lock.lock_save(flags);
	t->next = this->graveyard;
	this->graveyard = t;
	this->graveyard_lock.unlock_restore(flags);
}

void Scheduler::reap_graveyard(void)
{
	std::uint64_t flags;
	this->graveyard_lock.lock_save(flags);
	thread* g = this->graveyard;
	this->graveyard = nullptr;
	this->graveyard_lock.unlock_restore(flags);

	while (g)
	{
		thread* next = g->next;
		if (g->stack_owned && g->kernel_stack_base)
			memory::buddy.free_page(
			    memory::virt_to_phys(g->kernel_stack_base), 1);
		memory::slub.kfree(g);
		g = next;
	}
}

// Interrupt-context scheduling step; no lock is held across the switch.
interrupts::idt::frame* Scheduler::schedule(interrupts::idt::frame* f,
					    bool preempt)
{
	this->reap_graveyard();

	thread* current = current_thread();
	if (!current)
		return f;

	current->rsp = reinterpret_cast<std::uint64_t*>(f);

	bool requeue;
	if (preempt)
	{
		if (current->remaining_ticks > 0)
			current->remaining_ticks--;
		requeue = (current->remaining_ticks == 0);
	}
	else
	{
		requeue = true;
	}

	// A running thread may not be in any queue yet, so keep it on-CPU.
	if (!requeue)
		return f;

	std::uint64_t cpu = smp::this_cpu()->cpu_id;

	// Only runnable threads go back into a run queue.
	if (current->state == TaskState::READY ||
	    current->state == TaskState::RUNNING)
	{
		if (current->policy != Policy::IDLE)
		{
			current->remaining_ticks = DEFAULT_TIMESLICE;
			this->runqueues[cpu].enqueue(current);
		}
	}
	else if (current->state == TaskState::DEAD)
	{
		this->graveyard_push(current);
	}

	thread* next = this->runqueues[cpu].pick_next();

	if (next && next != current)
	{
		if (next->policy == Policy::IDLE)
			next->remaining_ticks = 1;
		else
			next->remaining_ticks = DEFAULT_TIMESLICE;

		next->state = TaskState::RUNNING;
		smp::this_cpu()->current_thread = next;
		return reinterpret_cast<interrupts::idt::frame*>(next->rsp);
	}

	if (current->policy == Policy::IDLE)
		current->remaining_ticks = 1;

	return f;
}

interrupts::idt::frame* Scheduler::tick(interrupts::idt::frame* f)
{
	__atomic_fetch_add(&ticks_since_boot, 1, __ATOMIC_RELAXED);

	std::uint64_t cpu = smp::this_cpu()->cpu_id;
	this->sleep_queues[cpu].wake_expired(
	    __atomic_load_n(&ticks_since_boot, __ATOMIC_RELAXED),
	    this->runqueues[cpu]);

	return this->schedule(f, true);
}

interrupts::idt::frame* Scheduler::yield(interrupts::idt::frame* f)
{
	return this->schedule(f, false);
}

void Scheduler::yield(void)
{
	asm volatile("int $0xFE");
}

void Scheduler::sleep(std::uint64_t ticks)
{
	thread* t = current_thread();
	if (!t)
		return;

	t->wake_tick =
	    __atomic_load_n(&ticks_since_boot, __ATOMIC_RELAXED) + ticks;
	this->sleep_queues[smp::this_cpu()->cpu_id].insert(t);
	this->yield();
}

void Scheduler::wake(thread* t)
{
	if (!t || t->cpu >= smp::MAX_CPUS)
		return;

	if (this->sleep_queues[t->cpu].remove(t))
		this->runqueues[t->cpu].enqueue(t);
}

[[noreturn]] void Scheduler::exit(void)
{
	thread* t = current_thread();
	if (t)
		t->state = TaskState::DEAD;

	this->yield();

	for (;;)
		asm volatile("hlt");
}

thread* Scheduler::create_thread(void (*entry)(void), Policy policy,
				 std::uint64_t cpu, std::uint64_t deadline)
{
	std::uint64_t stack_phys = memory::buddy.alloc_pages(1);
	if (!stack_phys)
		PANIC("create_thread: buddy alloc_pages failed");

	void* stack = memory::phys_to_virt(stack_phys);
	std::uint64_t stack_top = reinterpret_cast<std::uint64_t>(stack) + 8192;
	std::uint64_t* sp = reinterpret_cast<std::uint64_t*>(stack_top);

	// Initial frame layout matches isr_common in idt.asm.
	*--sp = 0x10;					  // SS
	*--sp = reinterpret_cast<std::uint64_t>(stack_top); // RSP
	*--sp = 0x202;					  // RFLAGS
	*--sp = 0x08;					  // CS
	*--sp = reinterpret_cast<std::uint64_t>(&thread_entry_trampoline); // RIP
	*--sp = 0;				    // error code
	*--sp = 0xFF;				    // vector
	for (int i = 0; i < 15; i++)
		*--sp = 0;

	thread* t = static_cast<thread*>(memory::slub.kmalloc(sizeof(thread)));
	if (!t)
		PANIC("create_thread: kmalloc failed");

	t->state = TaskState::READY;
	t->policy = policy;
	t->rsp = sp;
	t->kernel_stack_base = stack;
	t->next = nullptr;
	t->remaining_ticks = DEFAULT_TIMESLICE;
	t->cpu = cpu;
	t->deadline = deadline;
	t->wake_tick = 0;
	t->wait_next = nullptr;
	t->entry = entry;
	t->stack_owned = true;

	this->runqueues[cpu].enqueue(t);

	return t;
}

}
