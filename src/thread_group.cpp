#include <jfc/thread_group.h>

#include <moody/concurrentqueue.h>

#include <atomic>
#include <thread>

namespace jfc
{
    struct thread_group::shared_data_type
    {
        using task_collection_type = moodycamel::ConcurrentQueue<task_type>;

        /// \brief tasks are placed here and consumed by threads in the group.
        task_collection_type m_Tasks;

        /// \brief exit flag for the worker's loops. When the group falls out of scope (ignoring moves), the threads are told to exit.
        std::atomic<bool> m_GroupIsDestroyed = false;
    };

    size_t thread_group::thread_count() const
    {
        return m_Threads.size();
    }
    
    void thread_group::add_tasks(std::vector<thread_group::task_type> &&tasks)
    {
        m_SharedData->m_Tasks.enqueue_bulk(tasks.begin(), tasks.size());
    }
    void thread_group::add_tasks(thread_group::task_type &&task)
    {
        m_SharedData->m_Tasks.enqueue(std::move(task));
    }

    std::optional<thread_group::task_type> thread_group::try_get_task()
    {
        thread_group::task_type task;

        if (m_SharedData->m_Tasks.try_dequeue(task)) return task;

        return {};
    }

    thread_group::thread_id_collection_type thread_group::thread_ids() const
    {
        return m_Thread_IDs;
    }

    thread_group &thread_group::operator=(thread_group &&b) 
    {
        m_SharedData = std::move(b.m_SharedData);

        m_Threads = std::move(b.m_Threads);
        
        m_Thread_IDs = std::move(b.m_Thread_IDs);

        b.m_Threads.clear();

        return *this;
    }
    thread_group::thread_group(thread_group &&b) { (*this) = std::move(b); }

    thread_group::thread_group(size_t threadNumber) 
    : m_SharedData(std::make_shared<shared_data_type>())
    {
        m_Threads.reserve(threadNumber);

        auto shared = m_SharedData;   

        for (decltype(threadNumber) i(0); i < threadNumber; ++i) 
        {
            m_Threads.push_back(std::thread([shared]()
            {
                thread_group::task_type task;

                for (;;)
                {
                    if (shared->m_Tasks.try_dequeue(task))
                    {   
                        task();
                    }
                    else if (shared->m_GroupIsDestroyed.load(std::memory_order_relaxed)) break;
                }
            }));

            m_Thread_IDs.push_back(m_Threads.back().get_id());
        }
    }
    
    thread_group::thread_group()
    : thread_group(std::thread::hardware_concurrency() > 1 
        ? std::thread::hardware_concurrency() - 1
        : 0)
    {}

    thread_group::~thread_group()
    {  
        if (!m_Threads.empty())
        {
            m_SharedData->m_GroupIsDestroyed = true;

            for (auto &current_thread : m_Threads) current_thread.join();
        }
    }
}

