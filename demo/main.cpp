#include <jfc/thread_group.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace jfc;

std::unordered_map<std::thread::id, size_t> work_log;

void add_to_log(const std::thread::id id) {
    work_log[id] = work_log[id] + 1;
}

static constexpr size_t TASK_COUNT = 600000;

static constexpr size_t WAIT_TIME = 1000;

static constexpr size_t FAILING_TASK_COUNT = 3;

void sequential_impl() {
    std::cout << "sequential work begins...\n";

    const auto start_time(std::chrono::steady_clock::now());

    for (std::remove_const<decltype(TASK_COUNT)>::type i(0); i < TASK_COUNT; ++i)
    {
        add_to_log(std::this_thread::get_id());

        std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));
    }

    const auto end_time(std::chrono::steady_clock::now());

    std::cout 
        << "sequential work ends...\n"
        << "nano seconds taken: " << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n";
}

void concurrent_impl(size_t threadCount) {
    auto task_count = std::make_shared<std::atomic<size_t>>(TASK_COUNT);
    
    thread_group group(threadCount);
   
    work_log[std::this_thread::get_id()] = 0;

    for (const auto &id : group.thread_ids()) {
        work_log[id] = 0;
    }

    std::cout << "init begins...\n";
    
    const auto start_time(std::chrono::steady_clock::now());

    group.add_tasks({TASK_COUNT, [task_count]()
    {
        add_to_log(std::this_thread::get_id());

        std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));

        task_count->fetch_sub(1, std::memory_order_relaxed);
    }});

    std::cout << "init ends.\n";

    std::cout << "work begins...\n";

    while (task_count->load(std::memory_order_relaxed) > 0)
    {
        if (auto task = group.try_get_task()) (*task)();
    }

    const auto end_time(std::chrono::steady_clock::now());

    std::cout 
        << "work ends...\n"
        << "nano seconds taken: " << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n"
        << "# of threads in group: " << group.thread_count() << "\n";
}

void dispatcher_impl(size_t threadCount) {
    work_log.clear();

    thread_group_policy policy;

    thread_group group(threadCount, policy);

    work_log[std::this_thread::get_id()] = 0;

    for (const auto &id : group.thread_ids()) work_log[id] = 0;

    std::cout << "run_and_wait begins...\n";

    const auto start_time(std::chrono::steady_clock::now());

    std::vector<thread_group::task_type> tasks;

    tasks.reserve(TASK_COUNT + FAILING_TASK_COUNT);

    for (size_t i(0); i < TASK_COUNT; ++i)
        tasks.push_back([]()
        {
            add_to_log(std::this_thread::get_id());

            std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));
        });

    for (size_t i(0); i < FAILING_TASK_COUNT; ++i)
        tasks.push_back([]() { throw std::runtime_error("this task was always going to fail"); });

    group.run_and_wait(std::move(tasks));

    const auto end_time(std::chrono::steady_clock::now());

    const auto failures = policy.FAILED_TASKS->take();

    std::cout
        << "run_and_wait ends.\n"
        << "nano seconds taken: "
        << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n"
        << "# of threads in group: " << group.thread_count() << "\n"
        << "tasks that threw: " << failures.size()
        << " (dropped because the collection was full: " << policy.FAILED_TASKS->discarded() << ")\n";

    if (!failures.empty())
    {
        try { std::rethrow_exception(failures.front()); }
        catch (const std::exception &e) { std::cout << "first failure says: " << e.what() << "\n"; }
    }
}

int main(const int argc, const char **argv)
{
    if (argc != 2) throw std::invalid_argument("program requires 1 arg: number of threads! Special case: 0 indicates sequential implementation. all nonzero values indicate task based concurrent impl, even if only 1 thread is requested\n");

    auto thread_count = std::stoi(argv[1]);

    if (!thread_count) sequential_impl();
    else
    {
        concurrent_impl(thread_count - 1);
        dispatcher_impl(thread_count - 1);
    }

    size_t totalTaskCount(0);

    for (const auto &i : work_log) 
    {
        std::cout << i.first << ", " << i.second << "\n";

        totalTaskCount += i.second;
    }

    std::cout << "total tasks: " << totalTaskCount << "\n";

    return EXIT_SUCCESS;
}

