#pragma once

#include <cstdint>

namespace sched
{

struct thread;

namespace round_robin
{

class RoundRobin {
private:
	thread* head;
	thread* tail;

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
};

} // namespace round_robin

} // namespace sched
