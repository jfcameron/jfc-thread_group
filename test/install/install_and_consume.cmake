# © Joseph Cameron - MIT License

function(_run aWhat)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)

    if (_result)
        message(FATAL_ERROR "${aWhat} failed (${_result}):\n${_output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

set(_config_choice)

if (CONFIG)
    set(_config_choice --config "${CONFIG}")
endif()

_run("install" "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${WORK_DIR}/prefix" ${_config_choice})

set(_generator_choices)

if (PLATFORM)
    list(APPEND _generator_choices -A "${PLATFORM}")
endif()

if (TOOLSET)
    list(APPEND _generator_choices -T "${TOOLSET}")
endif()

_run("configuring the consumer" "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}"
    -B "${WORK_DIR}/build"
    -G "${GENERATOR}"
    ${_generator_choices}
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}"
    "-DCMAKE_PREFIX_PATH=${WORK_DIR}/prefix")

_run("building the consumer" "${CMAKE_COMMAND}" --build "${WORK_DIR}/build" ${_config_choice})

_run("running the consumer" "${WORK_DIR}/build/consumer")
