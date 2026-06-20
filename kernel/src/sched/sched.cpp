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

Scheduler scheduler;

void Scheduler::init(void)
{
	rr_sched.init();
	deadline_sched.init();
	realtime_sched.init();
	idle_sched.init();

	smp::cpu_info* bsp = smp::this_cpu();

	thread* initial = static_cast<thread*>(
	    memory::slub.kmalloc(sizeof(thread)));
	if (!initial)
		PANIC("sched_init: kmalloc failed");

	initial->state = TaskState::RUNNING;
	initial->policy = Policy::NORMAL;
	initial->rsp = nullptr;
	initial->kernel_stack_base = nullptr;
	initial->next = nullptr;
	initial->remaining_ticks = DEFAULT_TIMESLICE;
	initial->cpu = bsp->cpu_id;

	bsp->current_thread = initial;

	for (std::uint64_t i = 0; i < smp::MAX_CPUS; i++)
	{
		smp::cpu_info* cpu = &smp::cpu_infos[i];
		if (cpu == bsp)
			continue;

		thread* idle = this->idle_sched.get_idle(i);
		if (idle)
		{
			idle->state = TaskState::RUNNING;
			cpu->current_thread = idle;
		}
	}
}

void Scheduler::enqueue(thread* t)
{
	t->state = TaskState::READY;

	switch (t->policy)
	{
	case Policy::REALTIME:
	case Policy::DEADLINE:
		this->rr_sched.enqueue(t);
		break;
	case Policy::NORMAL:
		this->rr_sched.enqueue(t);
		break;
	case Policy::IDLE:
		break;
	}
}

thread* Scheduler::pick_next(void)
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

thread* Scheduler::get_idle(void)
{
	return this->idle_sched.get_idle(smp::this_cpu()->cpu_id);
}

interrupts::idt::frame* Scheduler::tick(interrupts::idt::frame* f)
{
	thread* current = current_thread();
	if (!current)
		return f;

	this->lock.lock();

	current->rsp = reinterpret_cast<std::uint64_t*>(f);

	if (current->remaining_ticks > 0)
		current->remaining_ticks--;

	if (current->remaining_ticks == 0)
	{
		if (current->policy != Policy::IDLE)
		{
			current->remaining_ticks = DEFAULT_TIMESLICE;
			this->enqueue(current);
		}

		thread* next = this->pick_next();

		if (next && next != current)
		{
			if (next->policy == Policy::IDLE)
				next->remaining_ticks = 1;
			else
				next->remaining_ticks = DEFAULT_TIMESLICE;

			next->state = TaskState::RUNNING;
			smp::this_cpu()->current_thread = next;
			this->lock.unlock();
			return reinterpret_cast<interrupts::idt::frame*>(
			    next->rsp);
		}

		if (current->policy == Policy::IDLE)
			current->remaining_ticks = 1;
	}

	this->lock.unlock();
	return f;
}

interrupts::idt::frame* Scheduler::yield(interrupts::idt::frame* f)
{
	thread* current = current_thread();
	if (!current)
		return f;

	this->lock.lock();

	current->rsp = reinterpret_cast<std::uint64_t*>(f);

	if (current->policy != Policy::IDLE)
	{
		current->remaining_ticks = DEFAULT_TIMESLICE;
		this->enqueue(current);
	}

	thread* next = this->pick_next();

	if (next && next != current)
	{
		if (next->policy == Policy::IDLE)
			next->remaining_ticks = 1;
		else
			next->remaining_ticks = DEFAULT_TIMESLICE;

		next->state = TaskState::RUNNING;
		smp::this_cpu()->current_thread = next;
		this->lock.unlock();
		return reinterpret_cast<interrupts::idt::frame*>(next->rsp);
	}

	this->lock.unlock();
	return f;
}

void Scheduler::yield(void)
{
	asm volatile("int $0xFE");
}

thread* Scheduler::create_thread(void (*entry)(void), Policy policy,
				 std::uint64_t cpu)
{
	std::uint64_t stack_phys = memory::buddy.alloc_pages(1);
	if (!stack_phys)
		PANIC("create_thread: buddy alloc_pages failed");

	void* stack = memory::phys_to_virt(stack_phys);
	std::uint64_t stack_top = reinterpret_cast<std::uint64_t>(stack) + 8192;
	std::uint64_t* sp = reinterpret_cast<std::uint64_t*>(stack_top);

	*--sp = 0x10;				   // SS (kernel data segment)
	*--sp = reinterpret_cast<std::uint64_t>(stack_top); // RSP
	*--sp = 0x202;					   // RFLAGS
	*--sp = 0x08;					   // CS
	*--sp = reinterpret_cast<std::uint64_t>(entry);	   // RIP
	*--sp = 0;					   // error code
	*--sp = 0xFF;					   // vector
	for (int i = 0; i < 15; i++)
		*--sp = 0;

	thread* t = static_cast<thread*>(
	    memory::slub.kmalloc(sizeof(thread)));
	if (!t)
		PANIC("create_thread: kmalloc failed");

	t->state = TaskState::READY;
	t->policy = policy;
	t->rsp = sp;
	t->kernel_stack_base = stack;
	t->next = nullptr;
	t->remaining_ticks = DEFAULT_TIMESLICE;
	t->cpu = cpu;

	std::uint64_t flags;
	this->lock.lock_save(flags);
	this->enqueue(t);
	this->lock.unlock_restore(flags);

	return t;
}

} // namespace sched
