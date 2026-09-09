#ifndef JFC_THREAD_GROUP_H
#define JFC_THREAD_GROUP_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace jfc
{
    /// \brief where exceptions thrown by tasks are put
    /// \warning Failures are silent until somebody looks.
    /// \remark thread safe. The mutex is only touched when a task actually throws, so it costs
    /// nothing on the path where they do not.
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

        /// \brief how many tasks a worker takes per trip to the queue
        const std::size_t TASKS_PER_DEQUEUE{8};

        /// \brief where exceptions escaping tasks are collected
        const std::shared_ptr<failed_task_collection> FAILED_TASKS
            = std::make_shared<failed_task_collection>();
    };

    /// \brief task-based concurrency abstraction.
    /// instantiates a number of threads at construction, provides tasks for them to execute via a synchronized queue.
    /// \remark all methods are thread friendly
    /// \remark all const methods are synchronization free
    /// \remark all mutable methods incur synchronization costs via atomic operations
    class thread_group final
    {
        public:
            /// \brief alias for task functor
            using task_type = std::function<void()>;

            /// \brief alias for thread collection
            using thread_collection_type = std::vector<std::thread>;

            /// \brief alias for thread id collection
            using thread_id_collection_type = std::vector<std::thread::id>;

        private:
            struct shared_data_type;

            /// \brief tell this group's workers to stop and wait for them
            void stop_and_join();
            
            std::shared_ptr<shared_data_type> m_SharedData; 
            thread_collection_type m_Threads;
            thread_id_collection_type m_Thread_IDs;

        public:
            /// \brief get the number of threads in the group
            size_t thread_count() const;

            /// brief returns a collection of IDs for the threads in the group
            thread_id_collection_type thread_ids() const;

            /// \brief adds a collection of tasks to the task collection
            void add_tasks(std::vector<task_type> &&tasks);
            /// \overload
            void add_tasks(task_type &&task);

            /// \brief run these tasks and return only once every one of them has finished
            void run_and_wait(std::vector<task_type> &&tasks);

            /// \brief run every task, taking aTasksPerDequeue of them at a time
            void run_and_wait(std::vector<task_type> &&tasks, std::size_t aTasksPerDequeue);

            /// \brief removes and returns a task if the task collection is nonzero.
            /// Use this to do task work from threads outside the group (typically this is the thread that created the group in the first place)
            std::optional<task_type> try_get_task();

            /// \brief drop every task that has not started yet, returning how many were dropped
            std::size_t cancel_pending();

        private:
            //! both run_and_wait forms, which differ only in what they ask the workers for
            void run_and_wait_at(std::vector<task_type> &&tasks, std::size_t aTasksPerDequeue);

        public:
            /// \brief supports move semantics
            thread_group &operator=(thread_group &&b);
            /// \brief supports move semantics
            thread_group(thread_group &&b); 

            /// \brief constructs a threadgroup with the specified number of threads.
            /// \throws std::invalid_argument if the policy asks for zero tasks per dequeue
            thread_group(size_t threadNumber, const thread_group_policy &aPolicy = {});

            /// \brief construct a thread group of size std::thread::hardware_concurrency() -1
            thread_group();

            /// \overload
            /// \brief a group of the same default size, with a policy of the caller's choosing
            explicit thread_group(const thread_group_policy &aPolicy);

            ~thread_group();
    };
}

#endif
