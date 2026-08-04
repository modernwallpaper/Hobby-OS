#include <sched/condvar.hpp>
#include <sched/sched.hpp>

namespace sched
{

CondVar::CondVar() : wait_head(nullptr), wait_tail(nullptr)
{
}

void CondVar::wait(Mutex& mutex)
{
	thread* me = current_thread();

	// Park first, then release the mutex, so the release cannot race a
	// signal into a lost wakeup.
	std::uint64_t flags;
	this->spinlock.lock_save(flags);

	me->state = TaskState::SLEEPING;
	me->wait_next = nullptr;
	if (this->wait_tail)
		this->wait_tail->wait_next = me;
	else
		this->wait_head = me;
	this->wait_tail = me;

	this->spinlock.unlock_restore(flags);

	mutex.unlock();
	scheduler.yield();
	mutex.lock();
}

void CondVar::signal(void)
{
	thread* w = nullptr;

	std::uint64_t flags;
	this->spinlock.lock_save(flags);

	if (this->wait_head)
	{
		w = this->wait_head;
		this->wait_head = w->wait_next;
		if (!this->wait_head)
			this->wait_tail = nullptr;
		w->wait_next = nullptr;
	}

	this->spinlock.unlock_restore(flags);

	if (w)
	{
		w->state = TaskState::READY;
		scheduler.enqueue(w);
	}
}

void CondVar::broadcast(void)
{
	thread* list = nullptr;

	std::uint64_t flags;
	this->spinlock.lock_save(flags);

	while (this->wait_head)
	{
		thread* next = this->wait_head->wait_next;
		this->wait_head->wait_next = list;
		list = this->wait_head;
		this->wait_head = next;
	}
	this->wait_tail = nullptr;

	this->spinlock.unlock_restore(flags);

	while (list)
	{
		thread* w = list;
		list = list->wait_next;
		w->wait_next = nullptr;
		w->state = TaskState::READY;
		scheduler.enqueue(w);
	}
}

} // namespace sched
