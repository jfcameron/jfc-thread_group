// © 2019 Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/thread_group.h>

#include <thread>

TEST_CASE( "jfc::thread_group test", "[jfc::thread_group]" )
{
    SECTION("default constructor creates group of expected size")
    {
        jfc::thread_group group;

        const auto expected_group_size = std::thread::hardware_concurrency() 
            ? std::thread::hardware_concurrency() - 1 
            : 0;

        REQUIRE(group.thread_count() == expected_group_size);
    }
    
    const int SIZE(4);

    jfc::thread_group group(SIZE);

    SECTION("User defined group size ctor produces group size specified by user")
    {
        REQUIRE(group.thread_count() == SIZE);
    }

    SECTION("task consumption works fine, enqueuing in bulk, individually. Tasks can be consumed outside the group as well.")
    {
        std::atomic<int> task_count(10);

        auto task = [&task_count]()
        {
            task_count.fetch_sub(1, std::memory_order_relaxed);
        };

        group.add_tasks({size_t(task_count - 1), task});

        group.add_tasks(task);

        while(task_count > 0)
        {
            if (auto task = group.try_get_task()) (*task)();
        }
    }

    SECTION("move semantics work as expected")
    {
        const auto id_count = group.thread_ids().size();
        
        jfc::thread_group moved_group(std::move(group));

        REQUIRE(!group.thread_count());
        REQUIRE(moved_group.thread_count());
        {
            jfc::thread_group another_moved_group = std::move(moved_group);
            
            REQUIRE(!moved_group.thread_count());
            REQUIRE(another_moved_group.thread_count());

            REQUIRE(another_moved_group.thread_ids().size() == id_count);
        }
    }
}

