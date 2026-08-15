# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(jfc-thread_group_consumer LANGUAGES CXX)

find_package(jfc-thread_group 1 REQUIRED)

add_executable(consumer main.cpp)

target_link_libraries(consumer PRIVATE jfc::thread_group)

set_target_properties(consumer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<0:>")
