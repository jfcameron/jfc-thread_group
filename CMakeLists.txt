# © Joseph Cameron - MIT License

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

if (TARGET jfc::thread_group)
    return()
endif()

project(jfc-thread_group
    VERSION 1.0
    DESCRIPTION "Tool for task-based concurrent programming: a thread group built around the lockless queue, moody concurrent queue project."
    LANGUAGES CXX)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR "jfc-thread_group needs GCC 11 or later, for std::atomic::wait")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

option(JFCTHREADGROUP_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(JFCTHREADGROUP_BUILD_DEMO "Build the demo" OFF)
option(JFCTHREADGROUP_BUILD_BENCH "Build the benchmark harness" OFF)
option(JFCTHREADGROUP_BUILD_DOCS "Build documentation" OFF)

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

include(GNUInstallDirs)

find_package(Threads REQUIRED)

add_library(jfc-thread_group STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thread_group.cpp")

add_library(jfc::thread_group ALIAS jfc-thread_group)

set_target_properties(jfc-thread_group PROPERTIES EXPORT_NAME thread_group)

target_include_directories(jfc-thread_group
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/extern")

target_compile_features(jfc-thread_group PUBLIC cxx_std_20)

target_link_libraries(jfc-thread_group PUBLIC Threads::Threads)

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

if (JFCTHREADGROUP_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if (JFCTHREADGROUP_BUILD_TESTS)
    add_subdirectory(test)
endif()

if (JFCTHREADGROUP_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if (JFCTHREADGROUP_BUILD_DOCS)
    jfc_generate_documentation_doxygen(
        README_PATH      README.md
        OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/docs

        INCLUDE_DIRECTORIES
            include/
    )
endif()
