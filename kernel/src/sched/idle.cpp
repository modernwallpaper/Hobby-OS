#include <cstdint>
#include <logging/logger.hpp>
#include <memory/memory.hpp>
#include <sched/sched.hpp>

extern "C" void idle_loop(void);

namespace sched
{

namespace idle
{

static thread idle_thread_data[smp::MAX_CPUS];
static char idle_stack_data[smp::MAX_CPUS][4096] __attribute__((aligned(16)));

void Idle::init(void)
{
	memory::memset(this->idle_threads, 0, sizeof(this->idle_threads));

	for (std::uint64_t i = 0; i < smp::MAX_CPUS; i++)
	{
		thread* t = &idle_thread_data[i];
		t->state = TaskState::READY;
		t->policy = Policy::IDLE;
		t->next = nullptr;
		t->remaining_ticks = 1;
		t->cpu = i;

		std::uint64_t stack_top =
		    reinterpret_cast<std::uint64_t>(&idle_stack_data[i + 1]);
		std::uint64_t* sp =
		    reinterpret_cast<std::uint64_t*>(stack_top);

		*--sp = 0x10;
		*--sp = stack_top;
		*--sp = 0x202;
		*--sp = 0x08;
		*--sp = reinterpret_cast<std::uint64_t>(::idle_loop);
		*--sp = 0;
		*--sp = 0xFF;
		for (int j = 0; j < 15; j++)
			*--sp = 0;

		t->rsp = sp;
		t->kernel_stack_base = &idle_stack_data[i];

		this->idle_threads[i] = t;
	}
}

thread* Idle::get_idle(std::uint64_t cpu)
{
	if (cpu >= smp::MAX_CPUS)
		return nullptr;
	return this->idle_threads[cpu];
}

}

}

extern "C" void idle_loop(void)
{
	for (;;)
		asm volatile("hlt");
}
