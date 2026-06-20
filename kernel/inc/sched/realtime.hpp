#pragma once

#include <cstdint>

namespace sched
{

struct thread;

namespace realtime
{

class Realtime {
public:
	void init(void);
	void enqueue(thread* t);
	thread* pick_next(void);
};

} // namespace realtime

} // namespace sched
