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

// Interrupt-safe spinlock. The raw lock()/unlock() pair is deliberately
// private: callers must go through lock_save()/unlock_restore() so that the
// lock is *never* held with interrupts enabled. Holding such a lock with IF
// set lets an interrupt fire on the same CPU and re-enter code that wants the
// same lock, which is an instant self-deadlock. lock_save() records whether
// IF was set and clears it before acquiring; unlock_restore() only re-enables
// interrupts if they were enabled on entry (a no-op inside an interrupt gate,
// where IF is already clear).
class IrqSpinlock {
public:
	IrqSpinlock() : locked(false)
	{
	}

	void lock_save(std::uint64_t& flags)
	{
		asm volatile("pushfq; popq %0; cli" : "=r"(flags));
		this->lock_impl();
	}

	void unlock_restore(std::uint64_t flags)
	{
		__atomic_store_n(&this->locked, false, __ATOMIC_RELEASE);
		if (flags & 0x200)
			asm volatile("sti");
	}

private:
	void lock_impl()
	{
		while (
		    __atomic_exchange_n(&this->locked, true, __ATOMIC_ACQUIRE))
			asm volatile("pause");
	}

	bool locked;
};

}
