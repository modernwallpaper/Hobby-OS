#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace sched
{

struct thread;

namespace realtime
{

class Realtime {
private:
	sync::IrqSpinlock lock;
	thread* head;
	thread* tail;

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
};

} // namespace realtime

} // namespace sched
