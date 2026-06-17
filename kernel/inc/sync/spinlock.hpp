#pragma once

namespace sync
{

class Spinlock {
public:
	Spinlock() : locked(false)
	{
	}

	void lock()
	{
		while (
		    __atomic_exchange_n(&this->locked, true, __ATOMIC_ACQUIRE))
			asm volatile("pause");
	}

	void unlock()
	{
		__atomic_store_n(&this->locked, false, __ATOMIC_RELEASE);
	}

private:
	bool locked;
};

} // namespace sync
