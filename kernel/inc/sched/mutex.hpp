#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace sched
{

struct thread;

// Blocking mutex. lock() parks the caller on the internal wait queue with the
// thread state set to SLEEPING if the mutex is contended, then yields until a
// waiter is woken by unlock(). The wait queue is FIFO but not strictly fair:
// a woken waiter re-races for the lock, so a new arrival can overtake it.
class Mutex {
public:
	Mutex();

	void lock(void);
	void unlock(void);

private:
	sync::IrqSpinlock spinlock;
	bool locked;
	thread* owner;
	thread* wait_head;
	thread* wait_tail;
};

} // namespace sched
