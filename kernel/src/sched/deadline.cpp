#include <sched/sched.hpp>

namespace sched
{

namespace deadline
{

void Deadline::init(void)
{
}

void Deadline::enqueue(thread* t)
{
	(void)t;
}

thread* Deadline::pick_next(void)
{
	return nullptr;
}

thread* Deadline::tick(thread* current)
{
	(void)current;
	return nullptr;
}

} // namespace deadline

} // namespace sched
