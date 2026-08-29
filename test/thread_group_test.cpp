// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/thread_group.h>

#include <atomic>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <thread>

TEST_CASE( "jfc::thread_group test", "[jfc::thread_group]" )
{
    SECTION("default constructor creates group of expected size")
    {
        jfc::thread_group group;

        const auto expected_group_size = std::thread::hardware_concurrency() 
            ? std::thread::hardware_concurrency() - 1 
            : 0;

        REQUIRE(group.thread_count() == expected_group_size);
    }
    
    const int SIZE(4);

    jfc::thread_group group(SIZE);

    SECTION("User defined group size ctor produces group size specified by user")
    {
        REQUIRE(group.thread_count() == SIZE);
    }

    SECTION("task consumption works fine, enqueuing in bulk, individually. Tasks can be consumed outside the group as well.")
    {
        std::atomic<int> task_count(10);

        auto task = [&task_count]()
        {
            task_count.fetch_sub(1, std::memory_order_relaxed);
        };

        group.add_tasks({size_t(task_count - 1), task});

        group.add_tasks(task);

        while(task_count > 0)
        {
            if (auto task = group.try_get_task()) (*task)();
        }
    }

    SECTION("move semantics work as expected")
    {
        const auto id_count = group.thread_ids().size();
        
        jfc::thread_group moved_group(std::move(group));

        REQUIRE(!group.thread_count());
        REQUIRE(moved_group.thread_count());
        {
            jfc::thread_group another_moved_group = std::move(moved_group);
            
            REQUIRE(!moved_group.thread_count());
            REQUIRE(another_moved_group.thread_count());

            REQUIRE(another_moved_group.thread_ids().size() == id_count);
        }
    }

    SECTION("move assignment onto a live group shuts the old one down instead of terminating")
    {
        jfc::thread_group live(2);
        jfc::thread_group other(3);

        const auto other_ids = other.thread_ids();

        live = std::move(other);

        REQUIRE(live.thread_count() == 3);
        REQUIRE(live.thread_ids() == other_ids);
        REQUIRE(!other.thread_count());

        std::atomic<std::size_t> done{0};

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 64; ++i)
            tasks.push_back([&done]() { done.fetch_add(1, std::memory_order_release); });

        live.add_tasks(std::move(tasks));

        while (done.load(std::memory_order_acquire) < 64)
            if (auto task = live.try_get_task()) (*task)();

        REQUIRE(done.load() == 64);
    }

    SECTION("run_and_wait returns only once every task has finished")
    {
        std::atomic<std::size_t> done{0};

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 500; ++i)
            tasks.push_back([&done]() { done.fetch_add(1, std::memory_order_release); });

        group.run_and_wait(std::move(tasks));

        REQUIRE(done.load(std::memory_order_acquire) == 500);
    }

    SECTION("run_and_wait works with no workers at all, by running everything on the caller")
    {
        jfc::thread_group sequential(0);

        REQUIRE(sequential.thread_count() == 0);

        std::atomic<std::size_t> done{0};

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 100; ++i)
            tasks.push_back([&done]() { done.fetch_add(1, std::memory_order_release); });

        sequential.run_and_wait(std::move(tasks));

        REQUIRE(done.load(std::memory_order_acquire) == 100);
    }

    SECTION("a task that throws neither hangs run_and_wait nor escapes it")
    {
        jfc::thread_group_policy policy;

        jfc::thread_group collecting(4, policy);

        std::atomic<std::size_t> succeeded{0};

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 50; ++i)
            tasks.push_back(i % 5 == 0
                ? jfc::thread_group::task_type([]() { throw std::runtime_error("task failed"); })
                : jfc::thread_group::task_type(
                    [&succeeded]() { succeeded.fetch_add(1, std::memory_order_release); }));

        REQUIRE_NOTHROW(collecting.run_and_wait(std::move(tasks)));

        REQUIRE(succeeded.load(std::memory_order_acquire) == 40);
        REQUIRE(policy.FAILED_TASKS->size() == 10);
    }

    SECTION("run_and_wait can be called from inside a task")
    {
        std::atomic<std::size_t> inner{0};

        std::vector<jfc::thread_group::task_type> outer;

        for (std::size_t i = 0; i < 4; ++i)
            outer.push_back([&group, &inner]() {
                std::vector<jfc::thread_group::task_type> nested;

                for (std::size_t k = 0; k < 10; ++k)
                    nested.push_back([&inner]() { inner.fetch_add(1, std::memory_order_release); });

                group.run_and_wait(std::move(nested));
            });

        group.run_and_wait(std::move(outer));

        REQUIRE(inner.load(std::memory_order_acquire) == 40);
    }

    SECTION("an empty batch is a no-op rather than a wait for nothing")
    {
        std::vector<jfc::thread_group::task_type> none;

        REQUIRE_NOTHROW(group.run_and_wait(std::move(none)));
    }

    SECTION("an exception escaping a task is collected rather than terminating")
    {
        jfc::thread_group_policy policy;

        std::atomic<std::size_t> succeeded{0};

        {
            jfc::thread_group group(4, policy);

            std::vector<jfc::thread_group::task_type> tasks;

            for (std::size_t i = 0; i < 40; ++i)
                tasks.push_back(i % 4 == 0
                    ? jfc::thread_group::task_type([]() { throw std::runtime_error("task failed"); })
                    : jfc::thread_group::task_type(
                        [&succeeded]() { succeeded.fetch_add(1, std::memory_order_release); }));

            group.add_tasks(std::move(tasks));

            while (succeeded.load(std::memory_order_acquire) + policy.FAILED_TASKS->size() < 40)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(succeeded.load() == 30);
        REQUIRE(policy.FAILED_TASKS->size() == 10);
        REQUIRE(policy.FAILED_TASKS->discarded() == 0);

        const auto failures = policy.FAILED_TASKS->take();

        REQUIRE(failures.size() == 10);
        REQUIRE(policy.FAILED_TASKS->size() == 0);   

        REQUIRE_THROWS_AS(std::rethrow_exception(failures.front()), std::runtime_error);
    }

    SECTION("the collection is bounded, and says how many it dropped")
    {
        const jfc::thread_group_policy policy{
            .FAILED_TASKS = std::make_shared<jfc::failed_task_collection>(8)};

        std::atomic<std::size_t> ran{0};

        {
            jfc::thread_group group(4, policy);

            std::vector<jfc::thread_group::task_type> tasks;

            for (std::size_t i = 0; i < 100; ++i)
                tasks.push_back([&ran]() {
                    ran.fetch_add(1, std::memory_order_release);

                    throw std::runtime_error("always fails");
                });

            group.add_tasks(std::move(tasks));

            while (ran.load(std::memory_order_acquire) < 100)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(policy.FAILED_TASKS->size() == 8);
        REQUIRE(policy.FAILED_TASKS->discarded() == 92);

        (void)policy.FAILED_TASKS->take();

        REQUIRE(policy.FAILED_TASKS->discarded() == 92);  
    }

    SECTION("self move assignment is a no-op rather than a shutdown")
    {
        jfc::thread_group self(2);

        const auto ids = self.thread_ids();

        self = std::move(self);

        REQUIRE(self.thread_count() == 2);
        REQUIRE(self.thread_ids() == ids);
    }
}


namespace {
    constexpr std::size_t WORKERS = 4;
    constexpr std::size_t TASKS = WORKERS + 1;
    constexpr auto TASK_DURATION = std::chrono::milliseconds(40);
}

TEST_CASE( "jfc::thread_group dequeue granularity", "[jfc::thread_group]" )
{
    const auto sleeping_tasks = [](std::atomic<std::size_t> &aRan) {
        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < TASKS; ++i)
            tasks.push_back([&aRan] {
                std::this_thread::sleep_for(TASK_DURATION);

                aRan.fetch_add(1, std::memory_order_relaxed);
            });

        return tasks;
    };

    const auto time_taken = [](auto &&aWork) {
        const auto began = std::chrono::steady_clock::now();

        aWork();

        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - began);
    };

    SECTION("a batch no wider than the group is spread across it, not swallowed by one worker")
    {
        jfc::thread_group group(WORKERS);

        std::atomic<std::size_t> ran = 0;

        const auto elapsed = time_taken([&] { group.run_and_wait(sleeping_tasks(ran)); });

        REQUIRE(ran.load() == TASKS);

        INFO("took " << elapsed.count() << " ms where serial would be "
            << (TASK_DURATION * TASKS).count() << " ms");

        REQUIRE(elapsed < TASK_DURATION * TASKS / 2);
    }

    SECTION("asking for one at a time explicitly does the same")
    {
        jfc::thread_group group(WORKERS);

        std::atomic<std::size_t> ran = 0;

        const auto elapsed = time_taken([&] { group.run_and_wait(sleeping_tasks(ran), 1); });

        REQUIRE(ran.load() == TASKS);
        REQUIRE(elapsed < TASK_DURATION * TASKS / 2);
    }

    SECTION("a block larger than the policy allows is capped, not honoured")
    {
        jfc::thread_group group(WORKERS, jfc::thread_group_policy{std::chrono::milliseconds{100}, 2});

        std::atomic<std::size_t> ran = 0;

        group.run_and_wait(sleeping_tasks(ran), 64);

        REQUIRE(ran.load() == TASKS);
    }

    SECTION("many small tasks still all run")
    {
        jfc::thread_group group(WORKERS);

        std::atomic<std::size_t> ran = 0;

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 10000; ++i)
            tasks.push_back([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

        group.run_and_wait(std::move(tasks));

        REQUIRE(ran.load() == 10000);
    }

    SECTION("asking for a block of zero is rejected")
    {
        jfc::thread_group group(WORKERS);

        std::vector<jfc::thread_group::task_type> tasks{[] {}};

        REQUIRE_THROWS_AS(group.run_and_wait(std::move(tasks), 0), std::invalid_argument);
    }
}

TEST_CASE( "jfc::thread_group cancel_pending", "[jfc::thread_group]" )
{
    SECTION("with no workers to run them, every queued task is dropped")
    {
        jfc::thread_group group(0);

        std::atomic<std::size_t> ran = 0;

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < 500; ++i)
            tasks.push_back([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

        group.add_tasks(std::move(tasks));

        REQUIRE(group.cancel_pending() == 500);
        REQUIRE(ran.load() == 0);
        REQUIRE(group.cancel_pending() == 0);
    }

    SECTION("cancelling does not strand a run_and_wait on work that will never run")
    {
        constexpr std::size_t TOTAL = 200;

        jfc::thread_group group(1);

        std::atomic<bool> go = false;
        std::atomic<std::size_t> ran = 0;
        std::atomic<bool> finished = false;

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < TOTAL; ++i)
            tasks.push_back([&go, &ran] {
                while (!go.load(std::memory_order_relaxed))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                ran.fetch_add(1, std::memory_order_relaxed);
            });

        std::thread runner([&] { group.run_and_wait(std::move(tasks)); finished = true; });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const auto dropped = group.cancel_pending();

        go = true;

        for (int i = 0; i < 500 && !finished.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        REQUIRE(finished.load());

        runner.join();

        INFO("dropped " << dropped << " and ran " << ran.load());

        REQUIRE(dropped > 0);
        REQUIRE(ran.load() + dropped == TOTAL);
    }
}
