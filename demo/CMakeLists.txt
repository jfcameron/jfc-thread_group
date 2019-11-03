# © 2018 Joseph Cameron - All Rights Reserved

jfc_project(executable
    NAME "jfc-thread_group-demo"
    VERSION 1.0
    DESCRIPTION "task-based concurrency tool."
    C++_STANDARD 17
    C_STANDARD 90

    SOURCE_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    
    PRIVATE_INCLUDE_DIRECTORIES
        "${jfc-thread_group_INCLUDE_DIRECTORIES}"

    LIBRARIES
        "${jfc-thread_group_LIBRARIES}"

    DEPENDENCIES
        "jfc-thread_group"
)
