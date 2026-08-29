#include <jfc/thread_group.h>

#include <moody/blockingconcurrentqueue.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace jfc
{
    failed_task_collection::failed_task_collection(std::size_t aCapacity)
    : m_Capacity(aCapacity)
    {}

    void failed_task_collection::add(std::exception_ptr aFailure)
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_Failures.size() >= m_Capacity)
        {
            ++m_Discarded;

            return;
        }

        m_Failures.push_back(std::move(aFailure));
    }

    std::vector<std::exception_ptr> failed_task_collection::take()
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);

        return std::move(m_Failures);
    }

    std::size_t failed_task_collection::size() const
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);

        return m_Failures.size();
    }

    std::size_t failed_task_collection::discarded() const
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);

        return m_Discarded;
    }

    struct thread_group::shared_data_type
    {
        using task_collection_type = moodycamel::BlockingConcurrentQueue<task_type>;

        task_collection_type m_Tasks;

        std::shared_ptr<failed_task_collection> m_Failures;

        std::atomic<bool> m_GroupIsDestroyed = false;

        std::atomic<std::size_t> m_TasksPerDequeue = 0;

        std::size_t m_MaxTasksPerDequeue = 1;

        std::size_t m_WorkerCount = 0;

        [[nodiscard]] std::size_t tasks_per_dequeue()
        {
            if (const auto requested = m_TasksPerDequeue.load(std::memory_order_relaxed))
                return std::min(requested, m_MaxTasksPerDequeue);

            const auto workers = m_WorkerCount ? m_WorkerCount : 1;

            const auto share = m_Tasks.size_approx() / workers;

            return std::clamp<std::size_t>(share, 1, m_MaxTasksPerDequeue);
        }
    };

    size_t thread_group::thread_count() const
    {
        return m_Threads.size();
    }
    
    void thread_group::add_tasks(std::vector<thread_group::task_type> &&tasks)
    {
        m_SharedData->m_Tasks.enqueue_bulk(std::make_move_iterator(tasks.begin()), tasks.size());
    }
    void thread_group::add_tasks(thread_group::task_type &&task)
    {
        m_SharedData->m_Tasks.enqueue(std::move(task));
    }

    void thread_group::run_and_wait(std::vector<thread_group::task_type> &&tasks)
    {
        run_and_wait_at(std::move(tasks), 0);
    }

    void thread_group::run_and_wait(std::vector<thread_group::task_type> &&tasks,
        const std::size_t aTasksPerDequeue)
    {
        if (!aTasksPerDequeue)
            throw std::invalid_argument(
                "jfc::thread_group: run_and_wait's aTasksPerDequeue must be at least 1");

        run_and_wait_at(std::move(tasks), aTasksPerDequeue);
    }

    void thread_group::run_and_wait_at(std::vector<thread_group::task_type> &&tasks,
        const std::size_t aTasksPerDequeue)
    {
        if (tasks.empty()) return;

        const auto previous = m_SharedData->m_TasksPerDequeue.exchange(aTasksPerDequeue,
            std::memory_order_relaxed);

        const struct restore_on_exit final
        {
            const std::shared_ptr<shared_data_type> &shared;
            const std::size_t previous;

            ~restore_on_exit()
            {
                shared->m_TasksPerDequeue.store(previous, std::memory_order_relaxed);
            }
        } restore{m_SharedData, previous};

        const auto remaining = std::make_shared<std::atomic<std::size_t>>(tasks.size());

        std::vector<thread_group::task_type> wrapped;

        wrapped.reserve(tasks.size());

        for (auto &task : tasks) {
            auto guard = std::shared_ptr<void>(nullptr, [remaining](void *) {
                if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) remaining->notify_all();
            });

            wrapped.push_back([task = std::move(task), guard = std::move(guard)]() {
                task();
            });
        }

        add_tasks(std::move(wrapped));

        for (;;)
        {
            const auto outstanding = remaining->load(std::memory_order_acquire);

            if (!outstanding) break;

            if (auto task = try_get_task())
            {
                try
                {
                    (*task)();
                }
                catch (...)
                {
                    if (!m_SharedData->m_Failures) throw;

                    m_SharedData->m_Failures->add(std::current_exception());
                }

                continue;
            }

            remaining->wait(outstanding, std::memory_order_acquire);
        }
    }

    std::size_t thread_group::cancel_pending()
    {
        std::size_t dropped = 0;

        std::vector<thread_group::task_type> block(m_SharedData->m_MaxTasksPerDequeue);

        for (;;)
        {
            const auto count = m_SharedData->m_Tasks.try_dequeue_bulk(block.begin(), block.size());

            if (!count) break;

            for (std::size_t i = 0; i < count; ++i) block[i] = nullptr;

            dropped += count;
        }

        return dropped;
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

    void thread_group::stop_and_join()
    {
        if (m_Threads.empty()) return;

        m_SharedData->m_GroupIsDestroyed = true;

        for (std::size_t i = 0; i < m_Threads.size(); ++i)
            m_SharedData->m_Tasks.enqueue([]{});

        for (auto &current_thread : m_Threads) current_thread.join();

        m_Threads.clear();

        m_Thread_IDs.clear();
    }

    thread_group &thread_group::operator=(thread_group &&b) 
    {
        if (this == &b) return *this;

        stop_and_join();

        m_SharedData = std::move(b.m_SharedData);

        m_Threads = std::move(b.m_Threads);
        
        m_Thread_IDs = std::move(b.m_Thread_IDs);

        b.m_Threads.clear();

        b.m_Thread_IDs.clear();

        return *this;
    }
    thread_group::thread_group(thread_group &&b) { (*this) = std::move(b); }

    thread_group::thread_group(size_t threadNumber, const thread_group_policy &aPolicy)
    : m_SharedData(std::make_shared<shared_data_type>())
    {
        if (!aPolicy.TASKS_PER_DEQUEUE)
            throw std::invalid_argument(
                "jfc::thread_group: thread_group_policy::TASKS_PER_DEQUEUE must be at least 1");

        m_Threads.reserve(threadNumber);

        auto shared = m_SharedData;   

        const auto tasksPerDequeue = aPolicy.TASKS_PER_DEQUEUE;

        m_SharedData->m_MaxTasksPerDequeue = tasksPerDequeue;
        m_SharedData->m_WorkerCount = threadNumber;
        const auto parkTimeout = aPolicy.PARK_TIMEOUT;
        const auto failures = aPolicy.FAILED_TASKS;

        m_SharedData->m_Failures = failures;

        try
        {
            for (decltype(threadNumber) i(0); i < threadNumber; ++i) 
            {
                m_Threads.push_back(std::thread([shared, tasksPerDequeue, parkTimeout, failures]()
                {
                    std::vector<thread_group::task_type> block(tasksPerDequeue);

                    for (;;)
                    {
                        const auto want = shared->tasks_per_dequeue();

                        auto count = shared->m_Tasks.try_dequeue_bulk(block.begin(), want);

                        if (!count)
                        {
                            if (shared->m_GroupIsDestroyed.load(std::memory_order_relaxed)) break;

                            count = shared->m_Tasks.wait_dequeue_bulk_timed(block.begin(),
                                want, parkTimeout);

                            if (!count) continue; 
                        }

                        for (std::size_t k = 0; k < count; ++k)
                        {
                            try
                            {
                                block[k]();
                            }
                            catch (...)
                            {
                                if (!failures) throw;

                                failures->add(std::current_exception());
                            }

                            block[k] = nullptr;
                        }
                    }
                }));

                m_Thread_IDs.push_back(m_Threads.back().get_id());
            }
        }
        catch (...)
        {
            stop_and_join(); 

            throw;
        }
    }
    
    namespace
    {
        [[nodiscard]] size_t default_thread_count()
        {
            return std::thread::hardware_concurrency() > 1
                ? std::thread::hardware_concurrency() - 1
                : 0;
        }
    }

    thread_group::thread_group()
    : thread_group(default_thread_count())
    {}

    thread_group::thread_group(const thread_group_policy &aPolicy)
    : thread_group(default_thread_count(), aPolicy)
    {}

    thread_group::~thread_group()
    {  
        stop_and_join();
    }
}
