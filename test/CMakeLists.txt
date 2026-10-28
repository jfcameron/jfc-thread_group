# © Joseph Cameron - MIT License

jfc_add_tests(
    C++_STANDARD 20

    TEST_SOURCE_FILES
        "${CMAKE_CURRENT_LIST_DIR}/thread_group_test.cpp"

    LIBRARIES
        jfc-thread_group
)

if (NOT CMAKE_CROSSCOMPILING)
    add_test(NAME jfc-thread_group_install_and_consume
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install_and_consume"
            "-DCONFIG=$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DEXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            -P "${CMAKE_CURRENT_LIST_DIR}/install/install_and_consume.cmake")
endif()
