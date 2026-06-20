#include <sched/sched.hpp>

namespace sched
{

namespace realtime
{

void Realtime::init(void)
{
}

void Realtime::enqueue(thread* t)
{
	(void)t;
}

thread* Realtime::pick_next(void)
{
	return nullptr;
}

} // namespace realtime

} // namespace sched
