#include <logging/logger.hpp>
#include <memory/buddy.hpp>
#include <memory/memory.hpp>
#include <memory/slub.hpp>
#include <sched/condvar.hpp>
#include <sched/deadline.hpp>
#include <sched/mutex.hpp>
#include <sched/realtime.hpp>
#include <sched/round_robin.hpp>
#include <sched/sched.hpp>
#include <tests/tests.hpp>

namespace tests
{

#define TEST_ASSERT(expr)                                                      \
	do                                                                     \
	{                                                                      \
		if (!(expr))                                                   \
		{                                                              \
			LOG("assert_failed; expr=%s; line=%d", #expr,          \
			    __LINE__);                                         \
			return false;                                          \
		}                                                              \
	} while (0)

namespace
{

class MemoryFunctionsTest final : public Test {
public:
	const char* name() const override
	{
		return "memory_functions";
	}

	bool run() override
	{
		std::uint8_t buffer[32];
		std::uint8_t source[32];

		for (std::uint64_t i = 0; i < sizeof(source); i++)
			source[i] = static_cast<std::uint8_t>(i);

		TEST_ASSERT(memory::memset(buffer, 0xA5, sizeof(buffer)) ==
			    buffer);
		for (std::uint64_t i = 0; i < sizeof(buffer); i++)
			TEST_ASSERT(buffer[i] == 0xA5);

		TEST_ASSERT(memory::memcpy(buffer, source, sizeof(source)) ==
			    buffer);
		TEST_ASSERT(memory::memcmp(buffer, source, sizeof(source)) ==
			    0);

		TEST_ASSERT(memory::memcmp(buffer, source, 0) == 0);
		buffer[7] = 0xFF;
		TEST_ASSERT(memory::memcmp(buffer, source, sizeof(source)) > 0);

		for (std::uint64_t i = 0; i < sizeof(buffer); i++)
			buffer[i] = static_cast<std::uint8_t>(i);

		TEST_ASSERT(memory::memmove(buffer + 4, buffer, 16) ==
			    buffer + 4);
		for (std::uint64_t i = 0; i < 16; i++)
			TEST_ASSERT(buffer[i + 4] == i);

		TEST_ASSERT(memory::memmove(buffer, buffer + 4, 16) == buffer);
		for (std::uint64_t i = 0; i < 16; i++)
			TEST_ASSERT(buffer[i] == i);

		return true;
	}
};

class BuddyAllocatorTest final : public Test {
public:
	const char* name() const override
	{
		return "buddy_allocator";
	}

	bool run() override
	{
		TEST_ASSERT(memory::buddy.alloc_pages(-1) == 0);
		TEST_ASSERT(memory::buddy.alloc_pages(memory::MAX_ORDER + 1) ==
			    0);

		std::uint64_t page = memory::buddy.alloc_pages(0);
		TEST_ASSERT(page != 0);
		TEST_ASSERT((page & (memory::PAGE_SIZE - 1)) == 0);

		std::uint64_t two_pages = memory::buddy.alloc_pages(1);
		TEST_ASSERT(two_pages != 0);
		TEST_ASSERT((two_pages & ((memory::PAGE_SIZE * 2) - 1)) == 0);
		TEST_ASSERT(page != two_pages);

		auto* page_ptr =
		    static_cast<std::uint8_t*>(memory::phys_to_virt(page));
		auto* two_page_ptr =
		    static_cast<std::uint8_t*>(memory::phys_to_virt(two_pages));

		page_ptr[0] = 0x11;
		page_ptr[memory::PAGE_SIZE - 1] = 0x22;
		two_page_ptr[0] = 0x33;
		two_page_ptr[(memory::PAGE_SIZE * 2) - 1] = 0x44;

		TEST_ASSERT(page_ptr[0] == 0x11);
		TEST_ASSERT(page_ptr[memory::PAGE_SIZE - 1] == 0x22);
		TEST_ASSERT(two_page_ptr[0] == 0x33);
		TEST_ASSERT(two_page_ptr[(memory::PAGE_SIZE * 2) - 1] == 0x44);

		memory::buddy.free_page(two_pages, 1);
		memory::buddy.free_page(page, 0);

		return true;
	}
};

class SlubAllocatorTest final : public Test {
public:
	const char* name() const override
	{
		return "slub_allocator";
	}

	bool run() override
	{
		void* small = memory::slub.kmalloc(24);
		TEST_ASSERT(small != nullptr);
		memory::memset(small, 0x5A, 24);
		memory::slub.kfree(small);

		auto* zeroed =
		    static_cast<std::uint8_t*>(memory::slub.kcalloc(17, 3));
		TEST_ASSERT(zeroed != nullptr);
		for (std::uint64_t i = 0; i < 51; i++)
			TEST_ASSERT(zeroed[i] == 0);
		memory::slub.kfree(zeroed);

		void* objects[32];
		for (std::uint64_t i = 0; i < 32; i++)
		{
			objects[i] = memory::slub.kmalloc(64);
			TEST_ASSERT(objects[i] != nullptr);
			for (std::uint64_t j = 0; j < i; j++)
				TEST_ASSERT(objects[i] != objects[j]);
		}

		for (std::uint64_t i = 0; i < 32; i++)
			memory::slub.kfree(objects[i]);

		auto* large =
		    static_cast<std::uint8_t*>(memory::slub.kmalloc(3000));
		TEST_ASSERT(large != nullptr);
		large[0] = 0x77;
		large[2999] = 0x88;
		TEST_ASSERT(large[0] == 0x77);
		TEST_ASSERT(large[2999] == 0x88);
		memory::slub.kfree(large);

		return true;
	}
};

class SchedulerQueuesTest final : public Test {
public:
	const char* name() const override
	{
		return "scheduler_queues";
	}

	bool run() override
	{
		sched::thread a{};
		sched::thread b{};
		sched::thread c{};

		sched::round_robin::RoundRobin rr;
		rr.init();
		TEST_ASSERT(rr.pick_next() == nullptr);
		rr.enqueue(&a);
		rr.enqueue(&b);
		rr.enqueue(&c);
		TEST_ASSERT(rr.pick_next() == &a);
		TEST_ASSERT(rr.pick_next() == &b);
		TEST_ASSERT(rr.pick_next() == &c);
		TEST_ASSERT(rr.pick_next() == nullptr);

		sched::realtime::Realtime rt;
		rt.init();
		rt.enqueue(&a);
		rt.enqueue(&b);
		TEST_ASSERT(rt.pick_next() == &a);
		TEST_ASSERT(rt.pick_next() == &b);
		TEST_ASSERT(rt.pick_next() == nullptr);

		a.deadline = 30;
		b.deadline = 10;
		c.deadline = 20;

		sched::deadline::Deadline dl;
		dl.init();
		dl.enqueue(&a);
		dl.enqueue(&b);
		dl.enqueue(&c);
		TEST_ASSERT(dl.pick_next() == &b);
		TEST_ASSERT(dl.pick_next() == &c);
		TEST_ASSERT(dl.pick_next() == &a);
		TEST_ASSERT(dl.pick_next() == nullptr);

		return true;
	}
};

MemoryFunctionsTest memory_functions_test;
BuddyAllocatorTest buddy_allocator_test;
SlubAllocatorTest slub_allocator_test;
SchedulerQueuesTest scheduler_queues_test;

namespace
{

// Shared state for the thread tests. volatile because the value is written
// by a worker thread and read after a context switch back to the test thread.
volatile bool scheduler_worker_ran = false;
volatile bool condvar_worker_ran = false;
volatile bool condvar_worker_done = false;
volatile bool condvar_predicate = false;

sched::Mutex test_mutex;
sched::CondVar test_condvar;

void scheduler_worker_entry(void)
{
	scheduler_worker_ran = true;
}

void condvar_worker_entry(void)
{
	test_mutex.lock();
	while (!condvar_predicate)
		test_condvar.wait(test_mutex);
	condvar_worker_ran = true;
	test_mutex.unlock();
	condvar_worker_done = true;
}

class SchedulerSleepTest final : public Test {
public:
	const char* name() const override
	{
		return "scheduler_sleep";
	}

	bool run() override
	{
		std::uint64_t before = sched::ticks_since_boot;
		sched::scheduler.sleep(50);
		std::uint64_t after = sched::ticks_since_boot;
		TEST_ASSERT(after - before >= 50);
		return true;
	}
};

class SchedulerThreadsTest final : public Test {
public:
	const char* name() const override
	{
		return "scheduler_threads";
	}

	bool run() override
	{
		sched::thread* t = sched::scheduler.create_thread(
		    scheduler_worker_entry, sched::Policy::NORMAL);
		TEST_ASSERT(t != nullptr);

		// switch to the worker; it sets the flag and exits itself
		sched::scheduler.yield();
		TEST_ASSERT(scheduler_worker_ran);

		// one more scheduling step reaps the exited worker
		sched::scheduler.yield();
		return true;
	}
};

class MutexCondvarTest final : public Test {
public:
	const char* name() const override
	{
		return "mutex_condvar";
	}

	bool run() override
	{
		sched::scheduler.create_thread(condvar_worker_entry,
					       sched::Policy::NORMAL);

		// let the worker acquire the mutex and park on the condvar
		sched::scheduler.yield();

		condvar_predicate = true;
		test_condvar.signal();

		// let the worker run to completion and exit
		sched::scheduler.yield();
		sched::scheduler.yield();

		TEST_ASSERT(condvar_worker_ran);
		TEST_ASSERT(condvar_worker_done);
		return true;
	}
};

SchedulerSleepTest scheduler_sleep_test;
SchedulerThreadsTest scheduler_threads_test;
MutexCondvarTest mutex_condvar_test;

}

Test* all_tests[] = {&memory_functions_test, &buddy_allocator_test,
		     &slub_allocator_test, &scheduler_queues_test,
		     &scheduler_sleep_test, &scheduler_threads_test,
		     &mutex_condvar_test};

}

void run_all(void)
{
	std::uint64_t passed = 0;
	std::uint64_t failed = 0;

	LOG("running_tests=%llu", sizeof(all_tests) / sizeof(all_tests[0]));

	for (auto* test : all_tests)
	{
		if (test->run())
		{
			passed++;
			LOG("test_pass=%s", test->name());
		}
		else
		{
			failed++;
			LOG("test_fail=%s", test->name());
		}
	}

	LOG("passed=%llu; failed=%llu", passed, failed);
}

}
