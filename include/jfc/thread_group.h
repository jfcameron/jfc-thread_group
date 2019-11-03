#ifndef JFC_THREAD_GROUP_H
#define JFC_THREAD_GROUP_H

#include <algorithm>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace jfc
{
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
            
            /// \brief shared data is stored in a shared_ptr to ensure it lives until the final thread participating in the consumption of the task collection has stopped doing work
            /// this refers to at least the number of threads in m_Threads, but because the queue is publicly accessible it also refers to external threads executing a functor returned by try_get_task after this thread_group has fallen out of scope
            std::shared_ptr<shared_data_type> m_SharedData; 

            /// \brief the threads in the group
            thread_collection_type m_Threads;

            /// \brief IDs of all threads contained in the group
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

            /// \brief removes and returns a task if the task collection is nonzero.
            /// this can be called publicly to allow threads outside the threadgroup to help perform its tasks (typically the thread which created the group in the first place)
            std::optional<task_type> try_get_task();

            /// \brief supports move semantics
            thread_group &operator=(thread_group &&b);
            /// \brief supports move semantics
            thread_group(thread_group &&b); 

            /// \brief constructs a threadgroup with the specified number of threads.
            thread_group(size_t threadNumber);

            /// \brief construct a thread group of size std::thread::hardware_concurrency() -1
            ///
            /// hardware_concurrency is a hint provided by the implementation about the # of threads that can be executed simultaneously on the hardware. The significance of a group of this size is that it represents a group that should be able to
            /// truly run concurrently on the system. The -1 refers to the thread responsible for creating the group, which cannot be captured by the group ctor, since that thread preceeds construction of the group. 
            /// If you want the creating thread (-1) to participate in the execution of the thread_group's tasks, call the method "try_get_task" where/whenever appropriate.
            /// for example, if blocking this thread is acceptable then you could call it in a loop directly after constructing the group and adding a collection of tasks to it.
            ///
            /// \warning when calling this ctor, it is possible for the resulting group to contain no threads. This will happen if your platform does not support multithreading. 
            /// In this case tasks can only be done via try_get_task. This can be checked via the method thread_count or the STL function thread::hardware_concurrency
            thread_group();

            ~thread_group();
    };
}

#endif
