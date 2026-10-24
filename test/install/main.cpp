// © Joseph Cameron - All Rights Reserved

#include <jfc/thread_group.h>

#if __has_include(<moody/concurrentqueue.h>)
#error "a private header was installed"
#endif

#include <atomic>
#include <stdexcept>
#include <vector>

int main() {
    jfc::thread_group group(2);

    std::atomic<int> ran = 0;

    std::vector<jfc::thread_group::task_type> tasks(10, [&ran] { ++ran; });

    tasks.push_back([] { throw std::runtime_error("rethrown"); });

    bool rethrown = false;

    try { group.run_and_wait(std::move(tasks)); }
    catch (const std::runtime_error &) { rethrown = true; }

    return ran == 10 && rethrown && group.pending() == 0 && group.failed_tasks() ? 0 : 1;
}
