// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/thread_group.h>

#include <atomic>
#include <string>
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

        while(task_count > 0) group.run_one();
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

        while (done.load(std::memory_order_acquire) < 64) live.run_one();

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

    SECTION("a task that throws does not hang run_and_wait, which rethrows it once the rest have run")
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

        REQUIRE_THROWS_AS(collecting.run_and_wait(std::move(tasks)), std::runtime_error);

        REQUIRE(succeeded.load(std::memory_order_acquire) == 40);
        REQUIRE(collecting.pending() == 0);
        REQUIRE(policy.FAILED_TASKS->size() == 0);
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
            jfc::thread_group failing(4, policy);

            std::vector<jfc::thread_group::task_type> tasks;

            for (std::size_t i = 0; i < 40; ++i)
                tasks.push_back(i % 4 == 0
                    ? jfc::thread_group::task_type([]() { throw std::runtime_error("task failed"); })
                    : jfc::thread_group::task_type(
                        [&succeeded]() { succeeded.fetch_add(1, std::memory_order_release); }));

            failing.add_tasks(std::move(tasks));

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
            jfc::thread_group bounded(4, policy);

            std::vector<jfc::thread_group::task_type> tasks;

            for (std::size_t i = 0; i < 100; ++i)
                tasks.push_back([&ran]() {
                    ran.fetch_add(1, std::memory_order_release);

                    throw std::runtime_error("always fails");
                });

            bounded.add_tasks(std::move(tasks));

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

        auto &same = self;

        self = std::move(same);

        REQUIRE(self.thread_count() == 2);
        REQUIRE(self.thread_ids() == ids);
    }
}


namespace {
    constexpr std::size_t WORKERS = 4;
    constexpr std::size_t TASKS = WORKERS + 1;
}

TEST_CASE( "jfc::thread_group dequeue granularity", "[jfc::thread_group]" )
{
    const auto meeting_tasks = [](std::atomic<std::size_t> &aRunning, std::atomic<std::size_t> &aMet) {
        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < TASKS; ++i)
            tasks.push_back([&aRunning, &aMet] {
                ++aRunning;

                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

                while (aRunning.load() < TASKS && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                if (aRunning.load() >= TASKS) ++aMet;
            });

        return tasks;
    };

    const auto meets_after_busy_workers = [&meeting_tasks](const jfc::thread_group_policy &aPolicy) {
        jfc::thread_group group(WORKERS, aPolicy);

        std::atomic<bool> go = false;
        std::atomic<std::size_t> busy = 0;

        group.add_tasks(std::vector<jfc::thread_group::task_type>(WORKERS, [&go, &busy] {
            ++busy;

            while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));

        for (int i = 0; i < 5000 && busy.load() < WORKERS; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const auto allBusy = busy.load() == WORKERS;

        std::atomic<std::size_t> running = 0, met = 0;

        std::thread runner([&] { group.run_and_wait(meeting_tasks(running, met)); });

        while (running.load() < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        go = true;

        runner.join();

        return allBusy ? met.load() : 0;
    };

    SECTION("a batch no wider than the group is spread across it, not swallowed by one worker")
    {
        REQUIRE(meets_after_busy_workers({}) == TASKS);
    }

    SECTION("and when workers may take eight at a time")
    {
        REQUIRE(meets_after_busy_workers(jfc::thread_group_policy{std::chrono::milliseconds{100}, 8}) == TASKS);
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

    SECTION("a policy of no tasks at a time is rejected")
    {
        REQUIRE_THROWS_AS(jfc::thread_group(WORKERS, jfc::thread_group_policy{std::chrono::milliseconds{100}, 0}),
            std::invalid_argument);
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

    SECTION("a run_and_wait's tasks are left to it, and the tasks queued beside them are dropped")
    {
        constexpr std::size_t TOTAL = 200;

        jfc::thread_group group(1);

        std::atomic<bool> go = false;
        std::atomic<bool> started = false;
        std::atomic<std::size_t> ran = 0;
        std::atomic<std::size_t> queuedRan = 0;
        std::atomic<bool> finished = false;

        group.add_tasks([&go, &started] {
            started = true;

            while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

        while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        group.add_tasks(std::vector<jfc::thread_group::task_type>(10, [&queuedRan] { ++queuedRan; }));

        std::atomic<bool> batchStarted = false;

        std::vector<jfc::thread_group::task_type> tasks;

        for (std::size_t i = 0; i < TOTAL; ++i)
            tasks.push_back([&go, &ran, &batchStarted] {
                batchStarted = true;

                while (!go.load(std::memory_order_relaxed))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                ran.fetch_add(1, std::memory_order_relaxed);
            });

        std::thread runner([&] { group.run_and_wait(std::move(tasks)); finished = true; });

        while (!batchStarted.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const auto dropped = group.cancel_pending();

        go = true;

        runner.join();

        REQUIRE(finished.load());
        REQUIRE(dropped == 10);
        REQUIRE(ran.load() == TOTAL);
        REQUIRE(queuedRan.load() == 0);

        for (int i = 0; i < 500 && group.pending(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(group.pending() == 0);
    }

    SECTION("it reaches every task not started, on a group of workers taking one at a time")
    {
        jfc::thread_group group(1);

        std::atomic<bool> go = false;
        std::atomic<bool> started = false;
        std::atomic<std::size_t> ran = 0;

        std::vector<jfc::thread_group::task_type> tasks{[&go, &started] {
            started = true;

            while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }};

        for (std::size_t i = 0; i < 10; ++i) tasks.push_back([&ran] { ++ran; });

        group.add_tasks(std::move(tasks));

        while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const auto dropped = group.cancel_pending();

        go = true;

        REQUIRE(dropped == 10);

        for (int i = 0; i < 500 && group.pending(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(ran.load() == 0);
        REQUIRE(group.pending() == 0);
    }
}

TEST_CASE( "jfc::thread_group run_and_wait waits on its own batch alone", "[jfc::thread_group]" )
{
    using namespace std::chrono_literals;

    constexpr std::size_t HANDED = 3;

    jfc::thread_group group(1, jfc::thread_group_policy{std::chrono::milliseconds{100}, 1});

    const auto caller = std::this_thread::get_id();

    std::atomic<std::size_t> handedRan = 0;
    std::atomic<bool> handedOnCaller = false;
    std::atomic<bool> started = false;
    std::atomic<bool> batchDone = false;

    std::vector<jfc::thread_group::task_type> handed;

    for (std::size_t i = 0; i < HANDED; ++i)
        handed.push_back([&, caller] {
            started = true;

            if (std::this_thread::get_id() == caller) handedOnCaller = true;

            const auto deadline = std::chrono::steady_clock::now() + 10s;

            while (!batchDone.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);

            handedRan.fetch_add(1);
        });

    group.add_tasks(std::move(handed));

    while (!started.load()) std::this_thread::sleep_for(1ms);

    std::atomic<std::size_t> batchRan = 0;

    std::vector<jfc::thread_group::task_type> batch;

    for (std::size_t i = 0; i < 20; ++i)
        batch.push_back([&batchRan] { batchRan.fetch_add(1); });

    group.run_and_wait(std::move(batch));

    const auto handedAtReturn = handedRan.load();

    batchDone = true;

    SECTION("the batch is done before the long work queued ahead of it")
    {
        REQUIRE(batchRan.load() == 20);
        REQUIRE(handedAtReturn == 0);
    }

    SECTION("the caller ran none of the long work")
    {
        REQUIRE_FALSE(handedOnCaller.load());
    }

    for (int i = 0; i < 400 && handedRan.load() < HANDED; ++i) std::this_thread::sleep_for(5ms);

    REQUIRE(handedRan.load() == HANDED);
    REQUIRE_FALSE(handedOnCaller.load());
}

TEST_CASE( "jfc::thread_group runs a fork-join's chunks by index", "[jfc::thread_group]" )
{
    jfc::thread_group group(3);

    std::vector<std::atomic<int>> ran(200);

    group.run_and_wait(ran.size(), [&ran](const std::size_t &aIndex) { ran[aIndex].fetch_add(1); });

    std::size_t once = 0;

    for (const auto &each : ran) if (each.load() == 1) ++once;

    REQUIRE(once == ran.size());

    SECTION("none at all is a no-op")
    {
        REQUIRE_NOTHROW(group.run_and_wait(0, [](const std::size_t &) { FAIL("ran a chunk of none"); }));
    }
}

TEST_CASE("a group says how many tasks are pending", "[jfc::thread_group][pending]")
{
    jfc::thread_group group(2);

    REQUIRE(group.pending() == 0);

    SECTION("counted up as tasks go in, and down as each finishes")
    {
        std::atomic<bool> release{false};
        std::atomic<int> finished{0};

        const auto wait_then_count = [&release, &finished]()
        {
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();

            finished.fetch_add(1, std::memory_order_relaxed);
        };

        group.add_tasks({wait_then_count, wait_then_count, wait_then_count});
        group.add_tasks(wait_then_count);

        REQUIRE(group.pending() == 4);

        release.store(true, std::memory_order_release);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (group.pending() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(group.pending() == 0);
        REQUIRE(finished.load() == 4);
    }

    SECTION("a task run outside the group counts off once it has run")
    {
        jfc::thread_group idle(0);

        idle.add_tasks([]{});

        REQUIRE(idle.pending() == 1);

        REQUIRE(idle.run_one());
        REQUIRE(idle.pending() == 0);
    }

    SECTION("a task that throws counts off too")
    {
        group.add_tasks([]{ throw std::runtime_error("in a task"); });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (group.pending() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(group.pending() == 0);
    }

    SECTION("cancelled tasks count off")
    {
        jfc::thread_group idle(0);

        idle.add_tasks({[]{}, []{}, []{}});

        REQUIRE(idle.pending() == 3);
        REQUIRE(idle.cancel_pending() == 3);
        REQUIRE(idle.pending() == 0);
    }
}


TEST_CASE("a moved-from group is left with no threads, and still works as one", "[jfc::thread_group]")
{
    jfc::thread_group source(2);

    SECTION("moved")
    {
        const jfc::thread_group destination(std::move(source));

        REQUIRE(destination.thread_count() == 2);
    }

    SECTION("move assigned")
    {
        jfc::thread_group destination(1);

        destination = std::move(source);

        REQUIRE(destination.thread_count() == 2);
    }

    REQUIRE(source.thread_count() == 0);
    REQUIRE(source.pending() == 0);
    REQUIRE(source.failed_tasks());

    std::atomic<int> ran{0};

    source.add_tasks([&ran] { ++ran; });

    REQUIRE(source.pending() == 1);

    REQUIRE(source.run_one());

    std::vector<jfc::thread_group::task_type> batch{[&ran] { ++ran; }, [&ran] { ++ran; }};

    source.run_and_wait(std::move(batch));

    source.add_tasks([&ran] { ++ran; });

    REQUIRE(source.cancel_pending() == 1);
    REQUIRE(ran.load() == 3);
    REQUIRE(source.pending() == 0);
}

TEST_CASE("run_one runs a queued task on the calling thread", "[jfc::thread_group][pending]")
{
    jfc::thread_group idle(0);

    SECTION("there, and counted off")
    {
        std::thread::id ranOn;

        idle.add_tasks([&ranOn] { ranOn = std::this_thread::get_id(); });

        REQUIRE(idle.run_one());
        REQUIRE(ranOn == std::this_thread::get_id());
        REQUIRE(idle.pending() == 0);
    }

    SECTION("one at a time")
    {
        std::atomic<int> ran{0};

        idle.add_tasks({[&ran] { ++ran; }, [&ran] { ++ran; }});

        REQUIRE(idle.run_one());
        REQUIRE(ran.load() == 1);
        REQUIRE(idle.pending() == 1);
    }

    SECTION("a failure goes to the group's collection")
    {
        idle.add_tasks([] { throw std::runtime_error("in a task run outside"); });

        REQUIRE(idle.run_one());
        REQUIRE(idle.failed_tasks()->size() == 1);
        REQUIRE(idle.pending() == 0);
    }

    SECTION("or with none, to whoever ran it")
    {
        jfc::thread_group uncollected(0, jfc::thread_group_policy{std::chrono::milliseconds{100}, 1, nullptr});

        uncollected.add_tasks([] { throw std::runtime_error("in a task run outside"); });

        REQUIRE_THROWS_AS(uncollected.run_one(), std::runtime_error);
        REQUIRE(uncollected.pending() == 0);
    }

    SECTION("an empty queue runs nothing")
    {
        REQUIRE_FALSE(idle.run_one());
        REQUIRE(idle.pending() == 0);
    }
}

TEST_CASE("run_one helps with a run_and_wait, and passes over what is left of one done", "[jfc::thread_group]")
{
    std::atomic<bool> go = false;
    std::atomic<bool> started = false;

    jfc::thread_group group(1);

    const struct release_on_exit final { std::atomic<bool> &go; ~release_on_exit() { go = true; } } release{go};

    group.add_tasks([&go, &started] {
        started = true;

        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });

    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    SECTION("helps")
    {
        std::atomic<bool> firstStarted = false;
        std::atomic<bool> letFirstGo = false;
        std::thread::id secondRanOn;

        std::vector<jfc::thread_group::task_type> batch{
            [&firstStarted, &letFirstGo] {
                firstStarted = true;

                while (!letFirstGo.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            },
            [&secondRanOn] { secondRanOn = std::this_thread::get_id(); }};

        std::thread runner([&] { group.run_and_wait(std::move(batch)); });

        while (!firstStarted.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const auto helped = group.run_one();

        letFirstGo = true;

        runner.join();

        REQUIRE(helped);
        REQUIRE(secondRanOn == std::this_thread::get_id());
    }

    SECTION("passes over")
    {
        std::vector<jfc::thread_group::task_type> batch(3, [] {});

        group.run_and_wait(std::move(batch));

        std::atomic<int> ran{0};

        group.add_tasks([&ran] { ++ran; });

        REQUIRE(group.run_one());
        REQUIRE(ran.load() == 1);
        REQUIRE_FALSE(group.run_one());
    }
}

TEST_CASE("run_and_wait gives its caller the first failure, once its batch is done", "[jfc::thread_group]")
{
    SECTION("whichever task threw first")
    {
        jfc::thread_group group(3);

        std::vector<jfc::thread_group::task_type> tasks;

        for (int i = 0; i < 20; ++i)
            tasks.push_back([i] { if (i % 7 == 3) throw std::runtime_error("failed " + std::to_string(i)); });

        try
        {
            group.run_and_wait(std::move(tasks));

            FAIL("nothing was rethrown");
        }
        catch (const std::runtime_error &e)
        {
            const std::string what = e.what();

            REQUIRE((what == "failed 3" || what == "failed 10" || what == "failed 17"));
        }

        REQUIRE(group.pending() == 0);
    }

    SECTION("with no workers and no collection, after running the rest")
    {
        jfc::thread_group alone(0, jfc::thread_group_policy{std::chrono::milliseconds{100}, 1, nullptr});

        std::atomic<int> ran{0};

        std::vector<jfc::thread_group::task_type> tasks{[] { throw std::runtime_error("first"); }};

        for (int i = 0; i < 4; ++i) tasks.push_back([&ran] { ++ran; });

        REQUIRE_THROWS_AS(alone.run_and_wait(std::move(tasks)), std::runtime_error);
        REQUIRE(ran.load() == 4);
        REQUIRE(alone.pending() == 0);
    }

    SECTION("no chunk runs after it has returned")
    {
        jfc::thread_group group(3, jfc::thread_group_policy{std::chrono::milliseconds{100}, 1, nullptr});

        std::atomic<std::size_t> ran{0};

        REQUIRE_THROWS_AS(group.run_and_wait(64, [&ran](const std::size_t &aIndex) {
            if (aIndex == 0) throw std::runtime_error("the first chunk");

            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            ++ran;
        }), std::runtime_error);

        const auto atReturn = ran.load();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        REQUIRE(atReturn == 63);
        REQUIRE(ran.load() == 63);
    }
}

TEST_CASE("pending counts a batch's tasks, and nothing of it once run_and_wait returns", "[jfc::thread_group][pending]")
{
    jfc::thread_group group(4);

    SECTION("every time")
    {
        for (int round = 0; round < 200; ++round)
        {
            std::vector<jfc::thread_group::task_type> tasks(64, [] {});

            group.run_and_wait(std::move(tasks));

            REQUIRE(group.pending() == 0);
        }
    }

    SECTION("while it runs, its own tasks are pending")
    {
        std::atomic<std::size_t> least{1000};

        std::vector<jfc::thread_group::task_type> tasks(8, [&group, &least]
        {
            const auto now = group.pending();

            for (auto seen = least.load(); now < seen && !least.compare_exchange_weak(seen, now);) {}
        });

        group.run_and_wait(std::move(tasks));

        REQUIRE(least.load() >= 1);
        REQUIRE(group.pending() == 0);
    }

    SECTION("and cancelled ones count off")
    {
        jfc::thread_group one(1);

        std::atomic<bool> go = false;

        std::vector<jfc::thread_group::task_type> tasks(100, [&go]
        {
            while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

        std::thread runner([&] { one.run_and_wait(std::move(tasks)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        static_cast<void>(one.cancel_pending());

        go = true;

        runner.join();

        REQUIRE(one.pending() == 0);
    }
}

TEST_CASE("a group says where its tasks' failures are collected", "[jfc::thread_group]")
{
    SECTION("its own collection, under the default policy, for add_tasks' tasks alone")
    {
        jfc::thread_group group(1);

        group.add_tasks([] { throw std::runtime_error("on a worker"); });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (group.pending() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::vector<jfc::thread_group::task_type> batch{[] { throw std::runtime_error("to the caller"); }};

        REQUIRE_THROWS_AS(group.run_and_wait(std::move(batch)), std::runtime_error);

        REQUIRE(group.failed_tasks());
        REQUIRE(group.failed_tasks()->size() == 1);
    }

    SECTION("the policy's, when it gives one")
    {
        const jfc::thread_group_policy policy;

        const jfc::thread_group group(1, policy);

        REQUIRE(group.failed_tasks() == policy.FAILED_TASKS);
    }

    SECTION("a separate one for each group")
    {
        const jfc::thread_group first(0), second(0);

        REQUIRE(first.failed_tasks() != second.failed_tasks());
    }
}
