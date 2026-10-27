foreach(iteration RANGE 1 2)
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" "${SCENARIO}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL "")
        message(FATAL_ERROR "Strategy failed: ${result}, ${error}\n${output}")
    endif()
    foreach(expected IN ITEMS "Signal    | event #1 | BUY ask" "Signal    | event #3 | SELL bid"
            "Paper complete | 2 requests | 0 rejected | 2 fills | fees=0.00020200 USDT"
            "total=100.00179800" "quantity=0.00100000 BTC"
            "Market #2 | elapsed=100 ns | execution BLOCKED"
            "cost basis=0.10010000 USDT | average cost=100.10 USDT | realized=+0.00000000 USDT"
            "realized=+0.00179800 USDT | unrealized=+0.00000000 USDT | total=+0.00179800 USDT")
        string(FIND "${output}" "${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Missing strategy result: ${expected}\n${output}")
        endif()
    endforeach()
    if(iteration EQUAL 2 AND NOT "${output}" STREQUAL "${previous}")
        message(FATAL_ERROR "Strategy replay is not deterministic")
    endif()
    set(previous "${output}")
endforeach()
file(STRINGS "${SESSION}" market_rows)
list(GET market_rows 0 market_header)
list(GET market_rows 1 first_snapshot)
set(prefix "${CMAKE_CURRENT_BINARY_DIR}/strategy-prefix.csv")
file(WRITE "${prefix}" "${market_header}\n${first_snapshot}\nend,1\n")
execute_process(COMMAND "${PROGRAM}" strategy "${prefix}" "${SCENARIO}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "1 requests"
        OR output MATCHES "SELL bid" OR NOT output MATCHES "total=0.00100000"
        OR NOT output MATCHES "unrealized=-0.00110000 USDT")
    message(FATAL_ERROR "Prefix replay must hold the partial buy without a future sell: ${output} ${error}")
endif()
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${SCENARIO}" RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Script command must reject strategy configuration")
endif()
file(READ "${SCENARIO}" config)
set(scratch "${CMAKE_CURRENT_BINARY_DIR}/strategy-invalid.csv")
foreach(row IN ITEMS "threshold,10200,10000,200000,1000000000" "order,1,1,buy,1000,100000"
        "threshold,10000,10200,1,1000000000" "threshold,10000,10200,200000,0")
    string(REGEX REPLACE "threshold,[^\n]+" "${row}" invalid "${config}")
    file(WRITE "${scratch}" "${invalid}")
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" "${scratch}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Invalid strategy configuration accepted")
    endif()
endforeach()
