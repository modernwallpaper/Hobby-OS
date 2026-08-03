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
	std::uint64_t flags;
	this->lock.lock_save(flags);

	t->next = nullptr;

	if (!this->head || t->deadline < this->head->deadline)
	{
		t->next = this->head;
		this->head = t;
		this->lock.unlock_restore(flags);
		return;
	}

	thread* prev = this->head;
	while (prev->next && prev->next->deadline <= t->deadline)
		prev = prev->next;

	t->next = prev->next;
	prev->next = t;

	this->lock.unlock_restore(flags);
}

thread* Deadline::pick_next(void)
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
	t->next = nullptr;

	this->lock.unlock_restore(flags);

	return t;
}

} // namespace deadline

} // namespace sched
