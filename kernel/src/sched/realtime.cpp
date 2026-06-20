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
	t->next = nullptr;

	if (this->tail)
		this->tail->next = t;
	else
		this->head = t;

	this->tail = t;
}

thread* Realtime::pick_next(void)
{
	thread* t = this->head;

	if (!t)
		return nullptr;

	this->head = t->next;

	if (!this->head)
		this->tail = nullptr;

	t->next = nullptr;

	return t;
}

} // namespace realtime

} // namespace sched
