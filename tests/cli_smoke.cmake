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
