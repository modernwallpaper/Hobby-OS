#include <sched/sched.hpp>

namespace sched
{

namespace deadline
{

void Deadline::init(void)
{
	this->head = nullptr;
}

void Deadline::enqueue(thread* t)
{
	t->next = nullptr;

	if (!this->head || t->deadline < this->head->deadline)
	{
		t->next = this->head;
		this->head = t;
		return;
	}

	thread* prev = this->head;
	while (prev->next && prev->next->deadline <= t->deadline)
		prev = prev->next;

	t->next = prev->next;
	prev->next = t;
}

thread* Deadline::pick_next(void)
{
	thread* t = this->head;

	if (!t)
		return nullptr;

	this->head = t->next;
	t->next = nullptr;

	return t;
}

} // namespace deadline

} // namespace sched
