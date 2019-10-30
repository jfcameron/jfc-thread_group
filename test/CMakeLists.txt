# © 2018 Joseph Cameron - All Rights Reserved

if(NOT CMAKE_SYSTEM_NAME MATCHES "Emscripten")
    jfc_add_tests(
        C++_STANDARD 17
        C_STANDARD 90

        TEST_SOURCE_FILES
            "${CMAKE_CURRENT_LIST_DIR}/super_cool_test.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/example_test.cpp"
    )
else()
    jfc_log(STATUS "test" "testing not supported on ${CMAKE_SYSTEM_NAME}. Skipping...")
endif()
