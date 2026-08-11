#include <sched/mutex.hpp>
#include <sched/sched.hpp>

namespace sched
{

Mutex::Mutex()
    : locked(false), owner(nullptr), wait_head(nullptr), wait_tail(nullptr)
{
}

void Mutex::lock(void)
{
	thread* me = current_thread();

	for (;;)
	{
		std::uint64_t flags;
		this->spinlock.lock_save(flags);

		if (!this->locked)
		{
			this->locked = true;
			this->owner = me;
			this->spinlock.unlock_restore(flags);
			return;
		}

		// Mark SLEEPING under the mutex lock to avoid timer requeue races.
		me->state = TaskState::SLEEPING;
		me->wait_next = nullptr;
		if (this->wait_tail)
			this->wait_tail->wait_next = me;
		else
			this->wait_head = me;
		this->wait_tail = me;

		this->spinlock.unlock_restore(flags);

		scheduler.yield();
	}
}

void Mutex::unlock(void)
{
	std::uint64_t flags;
	this->spinlock.lock_save(flags);

	if (this->wait_head)
	{
		thread* w = this->wait_head;
		this->wait_head = w->wait_next;
		if (!this->wait_head)
			this->wait_tail = nullptr;
		w->wait_next = nullptr;

		w->state = TaskState::READY;
		scheduler.enqueue(w);
	}

	this->locked = false;
	this->owner = nullptr;

	this->spinlock.unlock_restore(flags);
}

}
