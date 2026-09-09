# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(jfc-thread_group
    VERSION 1.0
    DESCRIPTION "Tool for task-based concurrent programming: a thread group built around the lockless queue, moody concurrent queue project."
    LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

option(JFC_BUILD_DEMO "Build the demo" ON)
option(JFC_BUILD_DOCS "Build documentation" ON)
option(JFC_BUILD_TESTS "Build unit tests" ON)
option(JFC_BUILD_BENCH "Build the benchmark harness" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic>)
    endif()

    include(CheckCXXCompilerFlag)

    foreach (_strict_flag "-Werror=c++20-extensions" "-Werror=c++23-extensions")
        string(MAKE_C_IDENTIFIER "HAS${_strict_flag}" _strict_flag_symbol)

        check_cxx_compiler_flag("${_strict_flag}" ${_strict_flag_symbol})

        if (${_strict_flag_symbol})
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_strict_flag}>)
        endif()
    endforeach()
endif()

find_package(Threads REQUIRED)

add_library(jfc-thread_group STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thread_group.cpp")

add_library(jfc::thread_group ALIAS jfc-thread_group)

set_target_properties(jfc-thread_group PROPERTIES EXPORT_NAME thread_group)

target_include_directories(jfc-thread_group
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/include")

target_compile_features(jfc-thread_group PUBLIC cxx_std_20)

target_link_libraries(jfc-thread_group PUBLIC Threads::Threads)

set(jfc-thread_group_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    CACHE PATH "deprecated: link the jfc-thread_group target instead" FORCE)

set(jfc-thread_group_LIBRARIES
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jfc-thread_group${CMAKE_STATIC_LIBRARY_SUFFIX}"
    ${CMAKE_THREAD_LIBS_INIT}
    CACHE PATH "deprecated: link the jfc-thread_group target instead" FORCE)

add_custom_command(TARGET jfc-thread_group POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:jfc-thread_group>"
        "${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_NAME:jfc-thread_group>")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS jfc-thread_group
    EXPORT jfc-thread_group-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT jfc-thread_group-targets
    FILE jfc-thread_group-targets.cmake
    NAMESPACE jfc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/jfc-thread_group")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/jfc-thread_group-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jfc-thread_group-config.cmake"
"include(CMakeFindDependencyMacro)
find_dependency(Threads)
include(\"\${CMAKE_CURRENT_LIST_DIR}/jfc-thread_group-targets.cmake\")
")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/jfc-thread_group-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/jfc-thread_group-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/jfc-thread_group")

if (JFC_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if (JFC_BUILD_TESTS)
    add_subdirectory(test)
endif()

if (JFC_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if (JFC_BUILD_DOCS)
    add_subdirectory(docs)
endif()
