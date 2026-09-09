# © Joseph Cameron - All Rights Reserved

jfc_add_tests(
    C++_STANDARD 20
    C_STANDARD 90

    TEST_SOURCE_FILES
        "${CMAKE_CURRENT_LIST_DIR}/thread_group_test.cpp"

    LIBRARIES
        jfc-thread_group
)
