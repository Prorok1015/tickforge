execute_process(COMMAND "${PROGRAM}" replay "${FIXTURE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL "")
    message(FATAL_ERROR "Book replay failed: ${result}, ${error}")
endif()
foreach(expected IN ITEMS "BID    | 64999.00 USDT x 0.00200000 BTC"
        "ASK    | 65002.00 USDT x 0.00500000 BTC"
        "GAP | book cleared" "WAITING | depth ignored" "IGNORED | update already covered"
        "synchronized at ID 112" "BID    | 65000.00 USDT x 0.00200000 BTC"
        "ASK    | 65003.00 USDT x 0.00300000 BTC"
        "8 events: 1 trades, 2 snapshots, 5 depth updates" "2 warnings")
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing expected console value: ${expected}\n${output}")
    endif()
endforeach()
execute_process(COMMAND "${PROGRAM}" replay "${FIXTURE}"
    RESULT_VARIABLE second_result OUTPUT_VARIABLE repeated ERROR_VARIABLE second_error)
if(NOT "${second_result}" STREQUAL "0" OR NOT "${second_error}" STREQUAL "" OR
   NOT "${output}" STREQUAL "${repeated}")
    message(FATAL_ERROR "Book replay is not deterministic")
endif()
