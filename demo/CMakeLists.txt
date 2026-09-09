# © Joseph Cameron - All Rights Reserved

add_executable(jfc-thread_group-demo
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

target_link_libraries(jfc-thread_group-demo PRIVATE jfc-thread_group)
