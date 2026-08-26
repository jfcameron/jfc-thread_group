# © Joseph Cameron - All Rights Reserved

if (MSVC)
    message(STATUS "jfc-thread_group: JFC_BUILD_BENCH is ON, but the bench needs gcc/clang extended "
        "asm. Skipping.")
    return()
endif()

find_package(Threads REQUIRED)

set(JFC_THREAD_GROUP_BENCH_TARGET "jfc-thread_group_bench")

add_executable(${JFC_THREAD_GROUP_BENCH_TARGET}
    "${CMAKE_CURRENT_LIST_DIR}/bench.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/thread_group.cpp")

target_include_directories(${JFC_THREAD_GROUP_BENCH_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/include")

target_link_libraries(${JFC_THREAD_GROUP_BENCH_TARGET} Threads::Threads)

set_target_properties(${JFC_THREAD_GROUP_BENCH_TARGET} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

target_compile_options(${JFC_THREAD_GROUP_BENCH_TARGET} PRIVATE -O2)
