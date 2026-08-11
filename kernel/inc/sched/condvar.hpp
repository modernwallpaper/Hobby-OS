#pragma once

#include <sched/mutex.hpp>

namespace sched
{

// Parks before releasing the mutex to close the lost-wakeup window.
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

}
