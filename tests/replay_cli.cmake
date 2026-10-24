set(previous "")
foreach(iteration RANGE 1 2)
    execute_process(COMMAND "${PROGRAM}" replay "${FIXTURE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL "" OR
       NOT output MATCHES "price=65000.00 USDT" OR
       NOT output MATCHES "quantity=0.00100000 BTC" OR
       NOT output MATCHES "3 events: 3 trades, 0 snapshots, 0 depth updates")
        message(FATAL_ERROR "Replay trace mismatch: result=${result}, stderr=${error}, stdout=${output}")
    endif()
    if(iteration EQUAL 2 AND NOT "${output}" STREQUAL "${previous}")
        message(FATAL_ERROR "Replay console output is not deterministic")
    endif()
    set(previous "${output}")
endforeach()
execute_process(COMMAND "${PROGRAM}" invalid RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "Invalid CLI arguments must return 2")
endif()
execute_process(COMMAND "${PROGRAM}" replay "${FIXTURE}.missing" RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Unreadable session must return 1")
endif()
