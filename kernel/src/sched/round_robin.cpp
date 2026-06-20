#include <logging/logger.hpp>
#include <sched/sched.hpp>

namespace sched
{

namespace round_robin
{

void RoundRobin::init(void)
{
	this->head = nullptr;
	this->tail = nullptr;
}

void RoundRobin::enqueue(thread* t)
{
	t->next = nullptr;

	if (this->tail)
		this->tail->next = t;
	else
		this->head = t;

	this->tail = t;
}

thread* RoundRobin::pick_next(void)
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

} // namespace round_robin

} // namespace sched
