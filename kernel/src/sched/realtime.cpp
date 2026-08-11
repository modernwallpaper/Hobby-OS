#include <sched/sched.hpp>

namespace sched
{

namespace realtime
{

void Realtime::init(void)
{
	this->head = nullptr;
	this->tail = nullptr;
}

void Realtime::enqueue(thread* t)
{
	std::uint64_t flags;
	this->lock.lock_save(flags);

	t->next = nullptr;

	if (this->tail)
		this->tail->next = t;
	else
		this->head = t;

	this->tail = t;

	this->lock.unlock_restore(flags);
}

thread* Realtime::pick_next(void)
{
	std::uint64_t flags;
	this->lock.lock_save(flags);

	thread* t = this->head;

	if (!t)
	{
		this->lock.unlock_restore(flags);
		return nullptr;
	}

	this->head = t->next;

	if (!this->head)
		this->tail = nullptr;

	t->next = nullptr;

	this->lock.unlock_restore(flags);

	return t;
}

}

}
