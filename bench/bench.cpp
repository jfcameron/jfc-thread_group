// © Joseph Cameron - All Rights Reserved

#include <jfc/thread_group.h>

#include <moody/concurrentqueue.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
    #include <sys/resource.h>
    #define JFC_BENCH_HAS_RUSAGE 1
#else
    #define JFC_BENCH_HAS_RUSAGE 0
#endif

namespace {
    using task_type = std::function<void()>;

    inline void clobber() { asm volatile("" : : : "memory"); }

    /// \brief lockless queue used by the library
    class moody_queue final {
        moodycamel::ConcurrentQueue<task_type> m_Queue;

    public:
        static constexpr const char *name = "moody";

        void enqueue(task_type &&aTask) { m_Queue.enqueue(std::move(aTask)); }

        void enqueue_bulk(std::vector<task_type> &aTasks) {
            m_Queue.enqueue_bulk(aTasks.begin(), aTasks.size());
        }

        bool try_dequeue(task_type &aOut) { return m_Queue.try_dequeue(aOut); }

        std::size_t try_dequeue_bulk(task_type *aOut, const std::size_t aMax) {
            return m_Queue.try_dequeue_bulk(aOut, aMax);
        }

        [[nodiscard]] std::size_t size_hint() { return m_Queue.size_approx(); }
    };

    /// \brief common stl based implementation to serve as a baseline
    class std_mutex_queue final {
        std::queue<task_type> m_Queue;
        std::mutex m_Mutex;

    public:
        static constexpr const char *name = "std";

        void enqueue(task_type &&aTask) {
            const std::lock_guard<std::mutex> lock(m_Mutex);

            m_Queue.push(std::move(aTask));
        }

        void enqueue_bulk(std::vector<task_type> &aTasks) {
            const std::lock_guard<std::mutex> lock(m_Mutex);

            for (auto &task : aTasks) m_Queue.push(std::move(task));
        }

        bool try_dequeue(task_type &aOut) {
            const std::lock_guard<std::mutex> lock(m_Mutex);

            if (m_Queue.empty()) return false;

            aOut = std::move(m_Queue.front());

            m_Queue.pop();

            return true;
        }

        std::size_t try_dequeue_bulk(task_type *aOut, const std::size_t aMax) {
            const std::lock_guard<std::mutex> lock(m_Mutex);

            std::size_t taken = 0;

            while (taken < aMax && !m_Queue.empty()) {
                aOut[taken++] = std::move(m_Queue.front());

                m_Queue.pop();
            }

            return taken;
        }

        [[nodiscard]] std::size_t size_hint() {
            const std::lock_guard<std::mutex> lock(m_Mutex);

            return m_Queue.size();
        }
    };

    template<typename queue_type>
    class bench_group final {
        struct shared_data_type {
            queue_type m_Tasks;
            std::atomic<bool> m_GroupIsDestroyed = false;

            std::mutex m_WaitMutex;
            std::condition_variable m_WorkAvailable;

            void wake(const bool aAll) {
                { const std::lock_guard<std::mutex> lock(m_WaitMutex); }

                if (aAll) m_WorkAvailable.notify_all();
                else m_WorkAvailable.notify_one();
            }
        };

        std::shared_ptr<shared_data_type> m_SharedData;
        std::vector<std::thread> m_Threads;
        std::size_t m_Drain;
        std::size_t m_Spins;  

    public:
        void add_tasks(std::vector<task_type> &&aTasks) {
            m_SharedData->m_Tasks.enqueue_bulk(aTasks);

            if (m_Spins) m_SharedData->wake(true);
        }

        void add_tasks(task_type &&aTask) {
            m_SharedData->m_Tasks.enqueue(std::move(aTask));

            if (m_Spins) m_SharedData->wake(false);
        }

        std::optional<task_type> try_get_task() {
            task_type task;

            if (m_SharedData->m_Tasks.try_dequeue(task)) return task;

            return {};
        }

        explicit bench_group(const std::size_t aThreadCount, const std::size_t aDrain = 1,
            const std::size_t aSpinsBeforePark = 0)
        : m_SharedData(std::make_shared<shared_data_type>())
        , m_Drain(aDrain)
        , m_Spins(aSpinsBeforePark) {
            m_Threads.reserve(aThreadCount);

            auto shared = m_SharedData;
            const auto drain = m_Drain;
            const auto threadCount = aThreadCount;
            const auto spins = m_Spins;

            for (std::size_t i = 0; i < aThreadCount; ++i)
                m_Threads.push_back(std::thread([shared, drain, threadCount, spins]() {
                    std::size_t idle = 0;

                    const auto wait_or_park = [&shared, spins, &idle]() {
                        if (!spins) return;                 

                        if (++idle < spins) return;

                        idle = 0;

                        std::unique_lock<std::mutex> lock(shared->m_WaitMutex);

                        if (!shared->m_GroupIsDestroyed.load(std::memory_order_relaxed))
                            shared->m_WorkAvailable.wait_for(lock, std::chrono::milliseconds(2));
                    };

                    if (drain <= 1) {
                        task_type task;

                        for (;;) {
                            if (shared->m_Tasks.try_dequeue(task)) { task(); idle = 0; }
                            else if (shared->m_GroupIsDestroyed.load(std::memory_order_relaxed)) break;
                            else wait_or_park();
                        }

                        return;
                    }

                    const auto adaptive = drain == 0;
                    const std::size_t cap = adaptive ? 256 : drain;

                    std::vector<task_type> block(cap);

                    for (;;) {
                        std::size_t want = cap;

                        if (adaptive) {
                            const auto share = shared->m_Tasks.size_hint() / (threadCount + 1);

                            want = share < 1 ? 1 : (share > cap ? cap : share);
                        }

                        const auto got = shared->m_Tasks.try_dequeue_bulk(block.data(), want);

                        if (got) {
                            for (std::size_t k = 0; k < got; ++k) {
                                block[k]();

                                block[k] = nullptr; 
                            }

                            idle = 0;
                        }
                        else if (shared->m_GroupIsDestroyed.load(std::memory_order_relaxed)) break;
                        else wait_or_park();
                    }
                }));
        }

        ~bench_group() {
            if (m_Threads.empty()) return;

            m_SharedData->m_GroupIsDestroyed = true;

            m_SharedData->wake(true); 

            for (auto &t : m_Threads) t.join();
        }
    };

    struct real_group final {
        jfc::thread_group group;

        explicit real_group(const std::size_t aThreads, const std::size_t aDrain = 1,
            const std::size_t = 0)
        : group(aThreads, jfc::thread_group_policy{
            .TASKS_PER_DEQUEUE = aDrain ? aDrain : 1})
        {}

        void add_tasks(std::vector<task_type> &&aTasks) { group.add_tasks(std::move(aTasks)); }

        void add_tasks(task_type &&aTask) { group.add_tasks(std::move(aTask)); }

        std::optional<task_type> try_get_task() { return group.try_get_task(); }
    };

    std::atomic<std::size_t> g_sink{0};

    template<typename body_type>
    [[nodiscard]] double best_ns_per_op(const std::size_t aOps, const std::size_t aPasses,
        body_type &&aBody) {
        double best = 1e30;

        for (std::size_t pass = 0; pass < aPasses; ++pass) {
            const auto start = std::chrono::steady_clock::now();

            aBody();

            clobber();

            const auto ns = std::chrono::duration<double, std::nano>(
                std::chrono::steady_clock::now() - start).count() / static_cast<double>(aOps);

            if (ns < best) best = ns;
        }

        return best;
    }

    [[nodiscard]] task_type trivial_task() { return []() { clobber(); }; }

    template<typename queue_type>
    [[nodiscard]] std::pair<double, double> uncontended(const std::size_t aOps,
        const std::size_t aPasses) {
        const auto enqueue = best_ns_per_op(aOps, aPasses, [&]() {
            queue_type q;

            for (std::size_t i = 0; i < aOps; ++i) q.enqueue(trivial_task());
        });

        const auto dequeue = best_ns_per_op(aOps, aPasses, [&]() {
            queue_type q;

            for (std::size_t i = 0; i < aOps; ++i) q.enqueue(trivial_task());

            task_type task;

            const auto start = std::chrono::steady_clock::now();
            (void)start;

            for (std::size_t i = 0; i < aOps; ++i) if (q.try_dequeue(task)) clobber();
        });

        return {enqueue, dequeue};
    }

    template<typename queue_type>
    [[nodiscard]] double contended(const std::size_t aProducers, const std::size_t aConsumers,
        const std::size_t aOps, const std::size_t aPasses) {
        return best_ns_per_op(aOps, aPasses, [&]() {
            queue_type q;

            std::atomic<std::size_t> consumed{0};
            std::atomic<bool> go{false};

            const auto perProducer = aOps / aProducers;

            std::vector<std::thread> threads;

            for (std::size_t p = 0; p < aProducers; ++p)
                threads.emplace_back([&]() {
                    while (!go.load(std::memory_order_acquire)) {}

                    for (std::size_t i = 0; i < perProducer; ++i) q.enqueue(trivial_task());
                });

            for (std::size_t c = 0; c < aConsumers; ++c)
                threads.emplace_back([&]() {
                    while (!go.load(std::memory_order_acquire)) {}

                    task_type task;

                    while (consumed.load(std::memory_order_relaxed) < perProducer * aProducers)
                        if (q.try_dequeue(task)) consumed.fetch_add(1, std::memory_order_relaxed);
                });

            go.store(true, std::memory_order_release);

            for (auto &t : threads) t.join();
        });
    }

    [[nodiscard]] task_type make_task(const std::size_t aSpin, std::atomic<std::size_t> &aDone) {
        return [aSpin, &aDone]() {
            std::size_t acc = 0;

            for (std::size_t i = 0; i < aSpin; ++i) { acc += i * 2654435761u; clobber(); }

            if (acc == 42) g_sink.fetch_add(1, std::memory_order_relaxed);

            aDone.fetch_add(1, std::memory_order_release);
        };
    }

    [[nodiscard]] double calibrate_spin(const std::size_t aSpin) {
        std::atomic<std::size_t> done{0};

        const auto task = make_task(aSpin, done);

        return best_ns_per_op(2000, 7, [&]() { for (std::size_t i = 0; i < 2000; ++i) task(); });
    }

    template<typename group_type>
    [[nodiscard]] double through_group(const std::size_t aThreads, const std::size_t aTasks,
        const std::size_t aSpin, const bool aBulk, const std::size_t aPasses,
        const std::size_t aDrain = 1, const std::size_t aSpins = 0) {
        group_type group(aThreads, aDrain, aSpins);

        return best_ns_per_op(aTasks, aPasses, [&]() {
            std::atomic<std::size_t> done{0};

            std::vector<task_type> tasks;
            tasks.reserve(aTasks);
            for (std::size_t i = 0; i < aTasks; ++i) tasks.push_back(make_task(aSpin, done));

            if (aBulk) group.add_tasks(std::move(tasks));
            else for (auto &t : tasks) group.add_tasks(std::move(t));

            while (done.load(std::memory_order_acquire) < aTasks)
                if (auto task = group.try_get_task()) (*task)();
        });
    }

    [[nodiscard]] double cpu_seconds() {
#if JFC_BENCH_HAS_RUSAGE
        rusage usage{};

        getrusage(RUSAGE_SELF, &usage);

        const auto to_s = [](const timeval &t) {
            return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) * 1e-6;
        };

        return to_s(usage.ru_utime) + to_s(usage.ru_stime);
#else
        return 0.0;
#endif
    }

    template<typename group_type>
    [[nodiscard]] double wake_latency_ns(const std::size_t aThreads, const std::size_t aDrain,
        const std::size_t aSpins, const std::size_t aSamples,
        const std::size_t aIdleMicroseconds = 3000) {
        group_type group(aThreads, aDrain, aSpins);

        std::vector<double> samples;
        samples.reserve(aSamples);

        for (std::size_t i = 0; i < aSamples; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(aIdleMicroseconds));

            std::atomic<bool> started{false};
            std::atomic<std::chrono::steady_clock::time_point> at{};

            const auto submitted = std::chrono::steady_clock::now();

            group.add_tasks(task_type([&started, &at]() {
                at.store(std::chrono::steady_clock::now(), std::memory_order_release);
                started.store(true, std::memory_order_release);
            }));

            while (!started.load(std::memory_order_acquire)) {}

            samples.push_back(std::chrono::duration<double, std::nano>(
                at.load(std::memory_order_acquire) - submitted).count());
        }

        std::sort(samples.begin(), samples.end());

        return samples[samples.size() / 2]; 
    }

    template<typename group_type>
    [[nodiscard]] double idle_cpu_seconds(const std::size_t aThreads, const std::size_t aMilliseconds,
        const std::size_t aDrain = 1, const std::size_t aSpins = 0) {
        group_type group(aThreads, aDrain, aSpins);

        const auto before = cpu_seconds();

        std::this_thread::sleep_for(std::chrono::milliseconds(aMilliseconds));

        return cpu_seconds() - before;
    }

    template<typename queue_type>
    [[nodiscard]] double empty_poll(const std::size_t aProducersSeen, const std::size_t aOps,
        const std::size_t aPasses) {
        queue_type q;

        {
            std::vector<std::thread> producers;

            for (std::size_t k = 0; k < aProducersSeen; ++k)
                producers.emplace_back([&]() { q.enqueue(trivial_task()); });

            for (auto &t : producers) t.join();
        }

        task_type drain;
        while (q.try_dequeue(drain)) {}                 

        return best_ns_per_op(aOps, aPasses, [&]() {
            task_type task;

            for (std::size_t i = 0; i < aOps; ++i) if (q.try_dequeue(task)) clobber();
        });
    }

    void heading(const char *aTitle) { std::printf("\n%s\n", aTitle); }

    void columns() { std::printf("  %-30s %9s %9s   %6s\n", "", "moody", "std", "ratio"); }

    void row(const std::string &aLabel, const double aMoody, const double aStd) {
        const auto ratio = aMoody > 0 ? aStd / aMoody : 0.0;

        std::printf("  %-30s %9.1f %9.1f   %5.2fx  %s\n", aLabel.c_str(), aMoody, aStd, ratio,
            ratio > 1.10 ? "moody" : (ratio < 0.91 ? "std" : "--"));
    }

    [[nodiscard]] std::string workers(const std::size_t n) {
        return std::to_string(n) + " worker" + (n == 1 ? "" : "s");
    }

    constexpr std::size_t LIBRARY_DRAIN = 8;
    constexpr std::size_t LIBRARY_SPINS = 16384;
}

int main(int argc, char **argv) {
    const std::size_t OPS = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 200000;
    const std::size_t PASSES = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 9;

    const auto hw = std::thread::hardware_concurrency();

    std::printf("jfc-thread_group: is the lock-free queue worth it?\n");
    std::printf("ops=%zu  passes=%zu  hardware_concurrency=%u\n", OPS, PASSES, hw);
    std::printf("ns per operation, lower is better. ratio = std / moody, so >1 means moody wins.\n");
    std::printf("every number is the best of %zu passes, which is the least noisy estimator here.\n",
        PASSES);
    std::printf("\nnote: the empty-task group row swings run to run and should not be read as a queue\n"
        "result. With free tasks every thread drains as fast as it can spin and the outcome is decided\n"
        "by how the scheduler happens to place them. Section B answers the uncontended-and-empty\n"
        "question properly, without threads in the way.\n");

    heading("A. the queue alone, one thread, no contention");
    columns();
    {
        const auto m = uncontended<moody_queue>(OPS, PASSES);
        const auto s = uncontended<std_mutex_queue>(OPS, PASSES);

        row("enqueue", m.first, s.first);
        row("enqueue + drain", m.second, s.second);
    }

    heading("B. the queue under contention, producers vs consumers");
    columns();
    for (const auto [p, c] : std::vector<std::pair<std::size_t, std::size_t>>{
            {1, 1}, {1, 3}, {3, 1}, {3, 3}})
        row(std::to_string(p) + "p / " + std::to_string(c) + "c",
            contended<moody_queue>(p, c, OPS, PASSES),
            contended<std_mutex_queue>(p, c, OPS, PASSES));

    const std::size_t SPIN_SMALL = 40;
    const std::size_t SPIN_LARGE = 400;

    const auto nsSmall = calibrate_spin(SPIN_SMALL);
    const auto nsLarge = calibrate_spin(SPIN_LARGE);

    const std::size_t GROUP_TASKS = 50000;

    std::vector<std::size_t> threadCounts{1, 2, 4};
    if (hw > 4) threadCounts.push_back(hw - 1);

    for (const auto [spin, ns] : std::vector<std::pair<std::size_t, double>>{
            {0, 0.0}, {SPIN_SMALL, nsSmall}, {SPIN_LARGE, nsLarge}}) {
        char title[128];

        if (spin == 0) std::snprintf(title, sizeof title,
            "C. through the group as the library ships it (drain 8, parking), empty task");
        else std::snprintf(title, sizeof title,
            "C. through the group as shipped, %.0fns of work per task", ns);

        heading(title);
        columns();

        for (const auto threads : threadCounts)
            row(workers(threads),
                through_group<bench_group<moody_queue>>(threads, GROUP_TASKS, spin, true, PASSES,
                    LIBRARY_DRAIN, LIBRARY_SPINS),
                through_group<bench_group<std_mutex_queue>>(threads, GROUP_TASKS, spin, true, PASSES,
                    LIBRARY_DRAIN, LIBRARY_SPINS));
    }

    heading("C. through the group as shipped, single enqueue rather than bulk, empty task");
    columns();
    for (const auto threads : threadCounts)
        row(workers(threads),
            through_group<bench_group<moody_queue>>(threads, GROUP_TASKS, 0, false, PASSES,
                LIBRARY_DRAIN, LIBRARY_SPINS),
            through_group<bench_group<std_mutex_queue>>(threads, GROUP_TASKS, 0, false, PASSES,
                LIBRARY_DRAIN, LIBRARY_SPINS));

    heading("D. cpu seconds burned by an IDLE group over 200ms of wall time");
    std::printf("  %-30s %9s %9s   (200ms wall)\n", "", "moody", "std");
    for (const auto threads : threadCounts)
        std::printf("  %-30s %9.3f %9.3f\n", workers(threads).c_str(),
            idle_cpu_seconds<bench_group<moody_queue>>(threads, 200),
            idle_cpu_seconds<bench_group<std_mutex_queue>>(threads, 200));

    heading("D2b. does a spin window catch work that arrives soon after the last task?");
    std::printf("  %-30s %12s %12s %12s\n", "", "never park", "park@64", "park@16384");
    for (const std::size_t idleUs : {5, 20, 50, 200})
        std::printf("  %-30s %9.0f ns %9.0f ns %9.0f ns\n",
            ("idle " + std::to_string(idleUs) + "us").c_str(),
            wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, 0, 60, idleUs),
            wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, 64, 60, idleUs),
            wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, 16384, 60, idleUs));

    heading("D2a. wake latency against how long the group sat idle (park after 64 spins)");
    std::printf("  %-30s %14s %14s\n", "", "spinning", "parking");
    for (const std::size_t idleUs : {50, 200, 1000, 3000, 20000})
        std::printf("  %-30s %11.0f ns %11.0f ns\n",
            ("idle " + std::to_string(idleUs) + "us before submit").c_str(),
            wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, 0, 40, idleUs),
            wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, 64, 40, idleUs));

    heading("D2. the same group, but parking after N idle spins (moody, 5 workers)");
    std::printf("  %-30s %14s %16s\n", "", "cpu s / 200ms", "wake latency ns");
    for (const std::size_t spins : {0, 1, 64, 1024, 16384}) {
        const auto cpu = idle_cpu_seconds<bench_group<moody_queue>>(hw - 1, 200, 8, spins);
        const auto lat = wake_latency_ns<bench_group<moody_queue>>(hw - 1, 8, spins, 40);

        std::printf("  %-30s %14.3f %16.0f\n",
            (spins == 0 ? std::string("never park (current)")
                        : "park after " + std::to_string(spins) + " spins").c_str(), cpu, lat);
    }

    heading("E. cost of finding the queue EMPTY, by how many threads have ever enqueued");
    columns();
    for (const std::size_t seen : {1, 2, 4, 8, 16})
        row(std::to_string(seen) + " producer" + (seen == 1 ? "" : "s") + " seen",
            empty_poll<moody_queue>(seen, OPS, PASSES),
            empty_poll<std_mutex_queue>(seen, OPS, PASSES));

    for (const auto [spin, ns] : std::vector<std::pair<std::size_t, double>>{
            {0, 0.0}, {SPIN_LARGE, nsLarge}}) {
        char title[160];

        if (spin == 0) std::snprintf(title, sizeof title,
            "F. tasks drained per trip to the queue, empty task");
        else std::snprintf(title, sizeof title,
            "F. tasks drained per trip to the queue, %.0fns of work per task", ns);

        heading(title);
        columns();

        for (const std::size_t drain : {1, 4, 16, 64, 256})
            row("drain " + std::to_string(drain),
                through_group<bench_group<moody_queue>>(hw - 1, GROUP_TASKS, spin, true, PASSES, drain),
                through_group<bench_group<std_mutex_queue>>(hw - 1, GROUP_TASKS, spin, true, PASSES, drain));
    }

    heading("G. drain size against batch size (moody, 124ns tasks, speedup vs drain 1)");
    std::printf("  %-14s %8s %8s %8s %8s %8s %8s %8s\n", "", "drain 1", "x4", "x8", "x16", "x64",
        "x256", "share");
    for (const std::size_t tasks : {64, 256, 1024, 8192, 50000}) {
        const auto base = through_group<bench_group<moody_queue>>(hw - 1, tasks, SPIN_LARGE, true, PASSES, 1);

        std::printf("  %-14s %8.1f", (std::to_string(tasks) + " tasks").c_str(), base);

        for (const std::size_t drain : {4, 8, 16, 64, 256, 0})
            std::printf(" %7.2fx",
                base / through_group<bench_group<moody_queue>>(hw - 1, tasks, SPIN_LARGE, true, PASSES, drain));

        std::puts("");
    }

    heading("H. what the library gained by draining in blocks (its own workload, small tasks)");
    std::printf("  %-30s %9s %9s   %6s\n", "", "drain 1", "drain 8", "gain");
    for (const auto threads : threadCounts) {
        const auto one = through_group<bench_group<moody_queue>>(threads, GROUP_TASKS, SPIN_SMALL, true, PASSES, 1);
        const auto eight = through_group<bench_group<moody_queue>>(threads, GROUP_TASKS, SPIN_SMALL, true, PASSES, LIBRARY_DRAIN);

        std::printf("  %-30s %9.1f %9.1f   %5.2fx\n", workers(threads).c_str(), one, eight, one / eight);
    }

    heading("control: the real jfc::thread_group against the copy at the same drain");
    std::printf("  %-30s %9s %9s   %6s\n", "", "real", "copy", "ratio");
    for (const auto threads : threadCounts) {
        const auto real = through_group<real_group>(threads, GROUP_TASKS, SPIN_SMALL, true, PASSES,
            LIBRARY_DRAIN);
        const auto copy = through_group<bench_group<moody_queue>>(threads, GROUP_TASKS, SPIN_SMALL,
            true, PASSES, LIBRARY_DRAIN);

        std::printf("  %-30s %9.1f %9.1f   %5.2fx\n", workers(threads).c_str(), real, copy,
            copy / real);
    }

    std::printf("\nsink %zu\n", g_sink.load());

    return 0;
}
