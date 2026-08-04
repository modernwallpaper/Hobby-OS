#pragma once

#include <sched/mutex.hpp>

namespace sched
{

// Condition variable. wait() atomically releases the given mutex, parks the
// caller on the condvar's wait queue, then yields; signal()/broadcast() wake
// one/all waiters, which re-acquire the mutex on resume. The classic
// lost-wakeup window is closed by parking the caller *before* releasing the
// mutex: a signal that runs after the release is guaranteed to observe the
// waiter already in the queue.
class CondVar {
public:
	CondVar();

	void wait(Mutex& mutex);
	void signal(void);
	void broadcast(void);

private:
	sync::IrqSpinlock spinlock;
	thread* wait_head;
	thread* wait_tail;
};

} // namespace sched
