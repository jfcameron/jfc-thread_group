# © Joseph Cameron - MIT License

add_executable(jfc-thread_group_bench
    "${CMAKE_CURRENT_LIST_DIR}/bench.cpp")

target_include_directories(jfc-thread_group_bench PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../extern")

target_link_libraries(jfc-thread_group_bench PRIVATE jfc-thread_group)

set_target_properties(jfc-thread_group_bench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
