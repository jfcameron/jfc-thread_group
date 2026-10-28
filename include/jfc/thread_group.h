// © Joseph Cameron - MIT License

#ifndef JFC_THREAD_GROUP_H
#define JFC_THREAD_GROUP_H

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

namespace jfc
{
    /// \brief where exceptions thrown by tasks given to add_tasks are put
    /// \warning failures are silent until somebody looks.
    /// \remark thread safe. The mutex is only touched when a task actually throws
    class failed_task_collection final
    {
        mutable std::mutex m_Mutex;

        std::vector<std::exception_ptr> m_Failures;

        std::size_t m_Capacity;

        std::size_t m_Discarded = 0;

        public:
            /// \brief record a failure, discarding it if the collection is already full
            void add(std::exception_ptr aFailure);

            /// \brief remove and return everything collected so far
            [[nodiscard]] std::vector<std::exception_ptr> take();

            //! how many failures are held, without removing them
            [[nodiscard]] std::size_t size() const;

            /// \brief how many failures were thrown away because the collection was full
            [[nodiscard]] std::size_t discarded() const;

            /// \param aCapacity how many failures to hold before discarding further ones
            explicit failed_task_collection(std::size_t aCapacity = 64);
    };

    struct thread_group_policy final
    {
        /// \brief how long a parked worker waits before looking again
        const std::chrono::milliseconds PARK_TIMEOUT{100};

        /// \brief how many tasks a worker takes per trip to the queue. More than one is quicker for tasks of tens of
        /// nanoseconds, but the tasks a worker has taken run one after another on it, and cancel_pending cannot reach them
        const std::size_t TASKS_PER_DEQUEUE{1};

        /// \brief where exceptions escaping tasks are collected
        const std::shared_ptr<failed_task_collection> FAILED_TASKS
            = std::make_shared<failed_task_collection>();
    };

    /// \brief task-based concurrency abstraction.
    /// instantiates a number of threads at construction, provides tasks for them to execute via a synchronized queue.
    /// \remark every method but the moves and the destructor may be called from any thread at once
    class thread_group final
    {
        public:
            /// \brief alias for task functor
            using task_type = std::function<void()>;

            /// \brief alias for thread id collection
            using thread_id_collection_type = std::vector<std::thread::id>;

        private:
            struct shared_data_type;

            [[nodiscard]] static std::shared_ptr<shared_data_type> make_shared_data(std::size_t aWorkerCount,
                const thread_group_policy &aPolicy);

            /// \brief tell this group's workers to stop and wait for them
            void stop_and_join();
            
            std::shared_ptr<shared_data_type> m_SharedData; 
            std::vector<std::thread> m_Threads;
            thread_id_collection_type m_Thread_IDs;

        public:
            /// \brief get the number of threads in the group
            [[nodiscard]] std::size_t thread_count() const;

            /// \brief returns a collection of IDs for the threads in the group
            [[nodiscard]] thread_id_collection_type thread_ids() const;

            /// \brief adds a collection of tasks to the task collection
            /// \throws std::bad_alloc if the queue cannot grow, and the tasks are not added
            void add_tasks(std::vector<task_type> &&tasks);
            /// \overload
            void add_tasks(task_type &&task);

            /// \brief run these tasks and return only once every one of them has finished
            /// \throws the first exception a task of these threw, once every one of them has finished
            void run_and_wait(std::vector<task_type> &&tasks);

            //! \brief one chunk of a fork-join
            using chunk_type = std::function<void(std::size_t aChunkIndex)>;

            /// \overload
            void run_and_wait(std::size_t aChunkCount, const chunk_type &aChunk);

            /// \brief runs one queued task on the calling thread, if there is one, and says whether there was. Lends a
            /// thread outside the group to its work. A failure goes to failed_tasks(), or, with none, to the caller
            bool run_one();

            /// \brief drop every task given to add_tasks that has not started yet, returning how many were dropped. A
            /// run_and_wait's tasks are left to it
            std::size_t cancel_pending();

            /// \brief how many tasks have been added and not yet finished: waiting in the queue, or
            /// running. Zero means every task handed over has run; a page can say it is still
            /// loading until then, where its workers start only once its main thread is free.
            [[nodiscard]] std::size_t pending() const;

            /// \brief where exceptions escaping tasks given to add_tasks are collected: the policy's FAILED_TASKS
            [[nodiscard]] std::shared_ptr<failed_task_collection> failed_tasks() const;

            /// \brief supports move semantics. The group moved from is left with no threads, and still works as one
            thread_group &operator=(thread_group &&b);
            /// \brief supports move semantics. The group moved from is left with no threads, and still works as one
            thread_group(thread_group &&b); 

            /// \brief constructs a threadgroup with the specified number of threads.
            /// \throws std::invalid_argument if the policy asks for zero tasks per dequeue
            thread_group(std::size_t threadNumber, const thread_group_policy &aPolicy = {});

            /// \brief construct a thread group of size std::thread::hardware_concurrency() -1
            thread_group();

            /// \overload
            /// \brief a group of the same default size, with a policy of the caller's choosing
            explicit thread_group(const thread_group_policy &aPolicy);

            ~thread_group();
    };
}

#endif
