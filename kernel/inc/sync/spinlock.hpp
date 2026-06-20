#pragma once

#include <cstdint>

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

class IrqSpinlock {
public:
	IrqSpinlock() : locked(false)
	{
	}

	void lock()
	{
		while (
		    __atomic_exchange_n(&this->locked, true, __ATOMIC_ACQUIRE))
			asm volatile("pause");
	}

	void lock_save(std::uint64_t& flags)
	{
		asm volatile("pushfq; popq %0; cli" : "=r"(flags));
		this->lock();
	}

	void unlock()
	{
		__atomic_store_n(&this->locked, false, __ATOMIC_RELEASE);
	}

	void unlock_restore(std::uint64_t flags)
	{
		__atomic_store_n(&this->locked, false, __ATOMIC_RELEASE);
		if (flags & 0x200)
			asm volatile("sti");
	}

private:
	bool locked;
};

} // namespace sync
