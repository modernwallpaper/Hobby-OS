#pragma once

#include <cstdint>

namespace sched
{

struct thread;

namespace deadline
{

class Deadline {
private:
	thread* head;

public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
};

} // namespace deadline

} // namespace sched
