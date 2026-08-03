#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace sched
{

struct thread;

namespace deadline
{

class Deadline {
private:
	sync::IrqSpinlock lock;
	thread* head;

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
};

} // namespace deadline

} // namespace sched
