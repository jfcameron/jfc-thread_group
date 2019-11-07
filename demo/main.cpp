#include <jfc/thread_group.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_map>

using namespace jfc;

std::unordered_map<std::thread::id, size_t> work_log;

void add_to_log(const std::thread::id id)
{
    work_log[id] = work_log[id] + 1;
}

static constexpr size_t TASK_COUNT = 600000;

static constexpr size_t WAIT_TIME = 1000;

/// \brief single thread performing the task TASK_COUNT # of times
void sequential_impl()
{
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

/// \brief thread_group impl, performing the task TASK_COUNT # of times (plus a few extras). Thread count is specified by the user
void concurrent_impl(size_t threadCount)
{
    auto task_count = std::make_shared<std::atomic<size_t>>(TASK_COUNT);
    
    thread_group group(threadCount);
   
    // creating keys for each ID, so we can safey write to the logging map in parallel
    work_log[std::this_thread::get_id()] = 0;

    for (const auto &id : group.thread_ids())
    {
        work_log[id] = 0;
    }

    // =-=- init -=--=
    std::cout << "init begins...\n";
    
    const auto start_time(std::chrono::steady_clock::now());

    group.add_tasks({TASK_COUNT, [task_count]()
    {
        add_to_log(std::this_thread::get_id());

        std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));

        task_count->fetch_sub(1, std::memory_order_relaxed);
    }});

    std::cout << "init ends.\n";

    // =-=- do work -=-=
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

int main(const int argc, const char **argv)
{
    if (argc != 2) throw std::invalid_argument("program requires 1 arg: number of threads! Special case: 0 indicates sequential implementation. all nonzero values indicate task based concurrent impl, even if only 1 thread is requested\n");

    auto thread_count = std::stoi(argv[1]);

    if (!thread_count) sequential_impl();
    else concurrent_impl(std::stoi(argv[1]) - 1);

    // =-=- print stats -=-=
    size_t totalTaskCount(0);

    for (const auto &i : work_log) 
    {
        std::cout << i.first << ", " << i.second << "\n";

        totalTaskCount += i.second;
    }

    std::cout << "total tasks: " << totalTaskCount << "\n";

    return EXIT_SUCCESS;
}

