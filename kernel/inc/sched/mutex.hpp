#pragma once

#include <cstdint>
#include <sync/spinlock.hpp>

namespace sched
{

struct thread;

// Blocking mutex; waiters are FIFO but re-race after wakeup.
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

}
