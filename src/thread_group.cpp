// © Joseph Cameron - MIT License

#include <jfc/thread_group.h>

#include <concurrentqueue/blockingconcurrentqueue.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iterator>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
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
        struct batch;

        struct entry final
        {
            task_type task;

            std::shared_ptr<batch> claim_of;

            entry() = default;

            entry(task_type &&aTask) noexcept
            : task(std::move(aTask))
            {}

            explicit entry(std::shared_ptr<batch> aBatch) noexcept
            : claim_of(std::move(aBatch))
            {}
        };

        using task_collection_type = moodycamel::BlockingConcurrentQueue<entry>;

        task_collection_type m_Tasks;

        std::shared_ptr<failed_task_collection> m_Failures;

        std::atomic<bool> m_GroupIsDestroyed = false;

        std::atomic<std::size_t> m_Pending = 0;

        std::size_t m_MaxTasksPerDequeue = 1;

        std::size_t m_WorkerCount = 0;

        [[nodiscard]] std::size_t tasks_per_dequeue()
        {
            const auto workers = m_WorkerCount ? m_WorkerCount : 1;

            const auto share = m_Tasks.size_approx() / workers;

            return std::clamp<std::size_t>(share, 1, m_MaxTasksPerDequeue);
        }

        void finished_one() { m_Pending.fetch_sub(1, std::memory_order_acq_rel); }

        [[nodiscard]] bool enqueue(task_type &&aTask)
        {
            m_Pending.fetch_add(1, std::memory_order_acq_rel);

            if (m_Tasks.enqueue(std::move(aTask))) return true;

            finished_one();

            return false;
        }

        void run(task_type &aTask)
        {
            try
            {
                aTask();
            }
            catch (...)
            {
                aTask = nullptr;

                finished_one();

                if (!m_Failures) throw;

                m_Failures->add(std::current_exception());

                return;
            }

            aTask = nullptr;

            finished_one();
        }
    };

    struct thread_group::shared_data_type::batch final
    {
        std::vector<thread_group::task_type> tasks;

        std::atomic<std::size_t> next = 0;

        std::atomic<std::size_t> remaining = 0;

        std::atomic<bool> failed = false;

        std::exception_ptr failure;

        std::shared_ptr<shared_data_type> shared;

        [[nodiscard]] thread_group::task_type *take()
        {
            const auto at = next.fetch_add(1, std::memory_order_relaxed);

            return at < tasks.size() ? &tasks[at] : nullptr;
        }

        void finished(thread_group::task_type &aTask)
        {
            aTask = nullptr;

            shared->finished_one();

            if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) remaining.notify_all();
        }

        bool run_next()
        {
            auto *const pTask = take();

            if (!pTask) return false;

            try
            {
                (*pTask)();
            }
            catch (...)
            {
                if (!failed.exchange(true, std::memory_order_relaxed)) failure = std::current_exception();
            }

            finished(*pTask);

            return true;
        }
    };

    namespace
    {
        [[nodiscard]] size_t default_thread_count()
        {
            return std::thread::hardware_concurrency() > 1
                ? std::thread::hardware_concurrency() - 1
                : 0;
        }
    }

    std::shared_ptr<thread_group::shared_data_type> thread_group::make_shared_data(const std::size_t aWorkerCount,
        const thread_group_policy &aPolicy)
    {
        auto out = std::make_shared<shared_data_type>();

        out->m_MaxTasksPerDequeue = aPolicy.TASKS_PER_DEQUEUE;
        out->m_WorkerCount = aWorkerCount;
        out->m_Failures = aPolicy.FAILED_TASKS;

        return out;
    }

    std::size_t thread_group::thread_count() const
    {
        return m_Threads.size();
    }

    void thread_group::add_tasks(std::vector<thread_group::task_type> &&tasks)
    {
        m_SharedData->m_Pending.fetch_add(tasks.size(), std::memory_order_acq_rel);

        if (m_SharedData->m_Tasks.enqueue_bulk(std::make_move_iterator(tasks.begin()), tasks.size())) return;

        m_SharedData->m_Pending.fetch_sub(tasks.size(), std::memory_order_acq_rel);

        throw std::bad_alloc();
    }
    void thread_group::add_tasks(thread_group::task_type &&task)
    {
        if (!m_SharedData->enqueue(std::move(task))) throw std::bad_alloc();
    }

    std::size_t thread_group::pending() const
    {
        return m_SharedData->m_Pending.load(std::memory_order_acquire);
    }

    std::shared_ptr<failed_task_collection> thread_group::failed_tasks() const
    {
        return m_SharedData->m_Failures;
    }

    void thread_group::run_and_wait(const std::size_t aChunkCount, const chunk_type &aChunk)
    {
        std::vector<thread_group::task_type> tasks;

        tasks.reserve(aChunkCount);

        for (std::size_t i = 0; i < aChunkCount; ++i) tasks.push_back([&aChunk, i] { aChunk(i); });

        run_and_wait(std::move(tasks));
    }

    void thread_group::run_and_wait(std::vector<thread_group::task_type> &&tasks)
    {
        if (tasks.empty()) return;

        const auto pBatch = std::make_shared<shared_data_type::batch>();

        pBatch->tasks = std::move(tasks);
        pBatch->remaining.store(pBatch->tasks.size(), std::memory_order_relaxed);
        pBatch->shared = m_SharedData;

        std::vector<shared_data_type::entry> claims(m_Threads.empty() ? 0 : pBatch->tasks.size(),
            shared_data_type::entry(pBatch));

        m_SharedData->m_Pending.fetch_add(pBatch->tasks.size(), std::memory_order_acq_rel);

        if (!claims.empty())
            static_cast<void>(m_SharedData->m_Tasks.enqueue_bulk(std::make_move_iterator(claims.begin()),
                claims.size()));

        while (pBatch->run_next()) {}

        for (auto outstanding = pBatch->remaining.load(std::memory_order_acquire); outstanding;
            outstanding = pBatch->remaining.load(std::memory_order_acquire))
            pBatch->remaining.wait(outstanding, std::memory_order_acquire);

        if (pBatch->failure) std::rethrow_exception(pBatch->failure);
    }

    std::size_t thread_group::cancel_pending()
    {
        std::size_t dropped = 0;

        std::vector<shared_data_type::entry> block(64);

        for (;;)
        {
            const auto count = m_SharedData->m_Tasks.try_dequeue_bulk(block.begin(), block.size());

            if (!count) break;

            std::size_t tasks = 0;

            for (std::size_t i = 0; i < count; ++i)
            {
                if (!block[i].claim_of) ++tasks;

                block[i] = {};
            }

            dropped += tasks;

            m_SharedData->m_Pending.fetch_sub(tasks, std::memory_order_acq_rel);
        }

        return dropped;
    }

    bool thread_group::run_one()
    {
        shared_data_type::entry taken;

        while (m_SharedData->m_Tasks.try_dequeue(taken))
        {
            if (taken.claim_of)
            {
                if (taken.claim_of->run_next()) return true;

                continue;
            }

            m_SharedData->run(taken.task);

            return true;
        }

        return false;
    }

    thread_group::thread_id_collection_type thread_group::thread_ids() const
    {
        return m_Thread_IDs;
    }

    void thread_group::stop_and_join()
    {
        if (m_Threads.empty()) return;

        m_SharedData->m_GroupIsDestroyed = true;

        for (std::size_t i = 0; i < m_Threads.size(); ++i) static_cast<void>(m_SharedData->enqueue([]{}));

        for (auto &current_thread : m_Threads) current_thread.join();

        m_Threads.clear();

        m_Thread_IDs.clear();
    }

    thread_group &thread_group::operator=(thread_group &&b)
    {
        if (this == &b) return *this;

        stop_and_join();

        m_SharedData = std::exchange(b.m_SharedData, make_shared_data(0, {}));

        m_Threads = std::move(b.m_Threads);

        m_Thread_IDs = std::move(b.m_Thread_IDs);

        b.m_Threads.clear();

        b.m_Thread_IDs.clear();

        return *this;
    }
    thread_group::thread_group(thread_group &&b) { (*this) = std::move(b); }

    thread_group::thread_group(std::size_t threadNumber, const thread_group_policy &aPolicy)
    : m_SharedData(make_shared_data(threadNumber, aPolicy))
    {
        if (!aPolicy.TASKS_PER_DEQUEUE)
            throw std::invalid_argument(
                "jfc::thread_group: thread_group_policy::TASKS_PER_DEQUEUE must be at least 1");

        m_Threads.reserve(threadNumber);

        auto shared = m_SharedData;

        const auto tasksPerDequeue = aPolicy.TASKS_PER_DEQUEUE;
        const auto parkTimeout = aPolicy.PARK_TIMEOUT;

        try
        {
            for (decltype(threadNumber) i(0); i < threadNumber; ++i)
            {
                m_Threads.push_back(std::thread([shared, tasksPerDequeue, parkTimeout]()
                {
                    std::vector<shared_data_type::entry> block(tasksPerDequeue);

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
                            auto taken = std::move(block[k]);

                            block[k] = {};

                            if (taken.claim_of) static_cast<void>(taken.claim_of->run_next());
                            else shared->run(taken.task);
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
