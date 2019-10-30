#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include <moody/concurrentqueue.h>

/// \brief collection of threads that perform tasks.
/// tasks are functors that are added to a synchronized collection of tasks. Threads then remove these tasks from the collection and execute them.
class thread_group final
{
public:
    using task_type = std::function<void()>;
    using task_collection_type = moodycamel::ConcurrentQueue<task_type>;
    using thread_collection_type = std::vector<std::thread>;

private:
    /// \brief the threads in the group
    thread_collection_type m_Threads;

    /// \brief shared data is stored in a shared_ptr to ensure it lives until the final thread participating in the consumption of the task collection has stopped doing work
    /// this refers to at least the number of threads in m_Threads, but because the queue is publicly accessible it also refers to external threads executing a functor returned by try_get_task after this thread_group has fallen out of scope
    struct shared_data_type
    {
        /// \brief tasks are placed here and consumed by threads in the group.
        task_collection_type m_Tasks;

        /// \brief exit flag for the worker's loops. When the group falls out of scope (ignoring moves), the threads are told to exit.
        std::atomic<bool> m_GroupIsDestroyed = false;
    };
    std::shared_ptr<shared_data_type> m_SharedData = std::make_shared<shared_data_type>();

public:
    /// \brief get the number of threads in the group
    size_t getThreadCount() const
    {
        return m_Threads.size();
    }

    /// \brief adds a collection of tasks to the task collection
    /// threadsafe. synchonization handled by queue impl
    void add_tasks(std::vector<task_type> &&tasks)
    {
        m_SharedData->m_Tasks.enqueue_bulk(tasks.begin(), tasks.size());
    }
    void add_tasks(task_type &&task)
    {
        m_SharedData->m_Tasks.enqueue(std::move(task));
    }

    /// \brief returns and removes a task from the task collection if the collection is nonzero.
    /// this can be called publicly to allow threads outside the threadgroup to help perform its tasks (typically the thread which created the group in the first place)
    /// threadsafe
    std::optional<task_type> try_get_task()
    {
        task_type task;

        if (m_SharedData->m_Tasks.try_dequeue(task)) return task;
        
        return {};
    }

    thread_group &operator=(thread_group &&b) 
    {
        m_SharedData = std::move(b.m_SharedData);

        m_Threads = std::move(b.m_Threads);

        b.m_Threads.clear();

        return *this;
    }
    thread_group(thread_group &&b) { (*this) = std::move(b); }

    /// \brief constructs a threadgroup with the specified number of threads.
    thread_group(size_t threadNumber) 
    {
        m_Threads.reserve(threadNumber);
       
        auto shared = m_SharedData;   

        for (decltype(threadNumber) i(0); i < threadNumber; ++i) 
        {
            m_Threads.push_back(std::thread([shared]()
            {
                task_type task;

                for (;;)
                {
                    if (shared->m_Tasks.try_dequeue(task))
                    {   
                        task();
                    }
                    else if (shared->m_GroupIsDestroyed.load(std::memory_order_relaxed)) break;
                }
            }));
        }
    }

    /// \brief construct a thread group of size std::thread::hardware_concurrency -1
    /// hardware_concurrency is a hint provided by the implementation about the # of threads that can be executed simultaneously on the hardware. The significance of a group of this size is it represents a group that should be able to
    /// truly run concurrently on the system. The -1 refers to the thread responsible for creating the group, which cannot be captured by the group since it preceeds the construction of the group. 
    /// If you want the creating thread (-1) to participate in the execution of the thread_group's tasks, call try_get_task where/whenever appropriate.
    /// for example, if blocking this thread is acceptable then you could call it in a loop directly after constructing the group and adding a collection of tasks to it.
    /// \warn calling this ctor, it is possible for a group to contain no threads. This will happen if your platform does not support multithreading. In this case tasks can only be done via try_get_task.
    thread_group()
    : thread_group(std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 0)
    {}

    ~thread_group()
    {  
        if (!m_Threads.empty())
        {
            m_SharedData->m_GroupIsDestroyed = true;

            for (auto &current_thread : m_Threads) current_thread.join();
        }
    }
};

// --==--==--==--==--==--==--==--==-==--==-
// TEST
// --==--==--==--==--==--==--==--==-==--==-

std::unordered_map<std::thread::id, size_t> work_log;

std::mutex map_mutex;

void add_to_log(const std::thread::id id)
{
    std::lock_guard<std::mutex> lock(map_mutex);

    work_log[id] = work_log[id] + 1;
}

using GROUP_TYPE = 
//thread_group;
thread_group; 

static constexpr size_t TASK_COUNT = 600000;

size_t WAIT_TIME = 1000;

/// \brief single thread performing the task TASK_COUNT # of times
void sequential_impl()
{
    std::cout << "sequential work begins...\n";

    const auto start_time(std::chrono::steady_clock::now());

    for (std::remove_const<decltype(TASK_COUNT)>::type i(0); i < TASK_COUNT; ++i)
    {
        //add_to_log(std::this_thread::get_id());

        for (int i = 10000;i > 0;--i){} //std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));
    }

    const auto end_time(std::chrono::steady_clock::now());

    std::cout << "sequential work ends...\n";

    std::cout << "nano seconds taken: " << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n";
}

/// \brief thread_group impl, performing the task TASK_COUNT # of times (plus a few extras). Thread count is specified by the user
void concurrent_impl(size_t threadCount)
{
    auto task_count = std::make_shared<std::atomic<size_t>>(TASK_COUNT);

    auto p = GROUP_TYPE(threadCount);
    {
        auto group = GROUP_TYPE(std::move(p)); //contrived but exercises move operator and ctor

        const auto start_time(std::chrono::steady_clock::now());
        // =-=- init -=--=
        {
            std::cout << "init begins...\n";

            group.add_tasks({TASK_COUNT, [task_count]()
            {
                //add_to_log(std::this_thread::get_id());

                for (int i = 10000;i > 0;--i){} //std::this_thread::sleep_for(std::chrono::nanoseconds(WAIT_TIME));

                task_count->fetch_add(-1, std::memory_order_relaxed);
            }});
        }
        std::cout << "   init ends.\n";

        // =-=- do work -=-=
        std::cout << "work begins...\n";

        while (task_count->load(std::memory_order_relaxed) > 0)
        {
            //for (int i = 0; i < 10000; ++i) 
            {
                if (auto task = group.try_get_task()) (*task)();
            }
        }

        const auto end_time(std::chrono::steady_clock::now());

        std::cout << "work ends...\n";

        // =-=- print stats -=-=
        size_t totalTaskCount(0);

        for (const auto &i : work_log) 
        {
            std::cout << i.first << ", " << i.second << "\n";

            totalTaskCount += i.second;
        }

        std::cout << "total tasks: " << totalTaskCount << "\n";

        std::cout << "# of threads in group: " << group.getThreadCount() << "\n";

        std::cout << "nano seconds taken: " << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n";
    }
}

void messing_around_with_tasks(int thread_count)
{
    thread_count - 1;

    std::cout << "start\n";

    struct shared_data_type
    {
        GROUP_TYPE group; //= GROUP_TYPE(thread_count);

        std::atomic<bool> shouldQuit = false; // flag indicating the test is done. should only be visible to the main function scope  and the endDraw task

        shared_data_type(int a)
            : group(a)

        {}
    };

    std::shared_ptr<shared_data_type> shared_data = std::make_shared<shared_data_type>(thread_count);

    auto endDraw = [=]()
    {
        std::cout << "cleaning up the glcontext after drawing the scene...\n";

        shared_data->shouldQuit = true;
    };

    // creates a batch of draw commands
    auto drawObjects = [=](int count)
    {
        std::shared_ptr<std::atomic<int>> shared_count = std::make_shared<std::atomic<int>>(count);

        std::vector<std::function<void()>> out;

        for (int objectID = 0; objectID < count; ++objectID)
        {
            out.push_back([=]()
                    {
                    float m(1), v(1), p(1);

                    auto mvp = p * v * m;

                    std::stringstream ss;

                    ss << "drawing object " << objectID << "\n";

                    std::cout << ss.str();

                    (*shared_count)--;

                    if (!(*shared_count))
                    {
                        shared_data->group.add_tasks(endDraw);
                    }
                    });
        }

        return out;
    };

    auto startDraw = [=]()
    {
        std::cout << "setting up glcontext for drawing...\n";

        shared_data->group.add_tasks(drawObjects(1000));
    };

    const auto start_time(std::chrono::steady_clock::now());

    shared_data->group.add_tasks(startDraw);

    while (!shared_data->shouldQuit)
    {
        if (auto task = shared_data->group.try_get_task()) (*task)();
    }

    const auto end_time(std::chrono::steady_clock::now());

    std::cout << "done\n";
    std::cout << "nano seconds taken: " << std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count() << "\n";
}

int main(const int argc, const char **argv)
{
    if (argc != 2) throw std::invalid_argument("program requires 1 arg: number of threads! Special case: 0 indicates sequential implementation. all nonzero values indicate task based concurrent impl, even if only 1 thread is requested\n");

    auto thread_count = std::stoi(argv[1]);

    if (!thread_count) sequential_impl();
    else concurrent_impl(std::stoi(argv[1]) - 1);

    //messing_around_with_tasks(thread_count - 1);

    return EXIT_SUCCESS;
}

