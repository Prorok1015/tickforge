execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "CLI failed (${result}): ${error}")
endif()
if(NOT output MATCHES "tickforge [|] Market research engine" OR
   NOT output MATCHES "SIMULATION ONLY" OR NOT output MATCHES "replay SESSION")
    message(FATAL_ERROR "Unexpected CLI output: ${output}")
endif()
if(NOT "${error}" STREQUAL "")
    message(FATAL_ERROR "Unexpected CLI stderr: ${error}")
endif()
if(NOT output MATCHES "live --config FILE.toml")
    message(FATAL_ERROR "Usage must list the live command: ${output}")
endif()
# live argument errors are found before any network access.
foreach(case IN ITEMS "2|live" "2|live;--duration;5" "2|live;--config;a.toml;--plain;--plain"
        "1|live;--config;a.toml;--duration;0" "1|live;--config;a.toml;--duration;86401")
    string(FIND "${case}" "|" bar)
    string(SUBSTRING "${case}" 0 ${bar} expected)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${case}" ${start} -1 arguments)
    execute_process(COMMAND "${PROGRAM}" ${arguments} RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "${expected}")
        message(FATAL_ERROR "live ${arguments} must exit ${expected}, got ${result}: ${error}")
    endif()
endforeach()
