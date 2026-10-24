set(previous "")
foreach(iteration RANGE 1 2)
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${SCENARIO}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0" OR NOT "${error}" STREQUAL "")
        message(FATAL_ERROR "Paper scenario failed: ${result}, ${error}")
    endif()
    foreach(expected IN ITEMS "Paper complete | 8 requests | 1 rejected | 8 fills | fees=0.78001350 USDT"
            "total=609.20148650 | reserved=0.00000000 | available=609.20148650"
            "total=0.00600000 | reserved=0.00000000 | available=0.00600000"
            "quantity=0.00153000 BTC" "quantity=0.00047000 BTC"
            "PARTIAL (remainder cancelled)" "book unavailable or stale" "price=64998.00 USDT")
        string(FIND "${output}" "${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Paper output missing: ${expected}\n${output}")
        endif()
    endforeach()
    if(iteration EQUAL 2 AND NOT "${previous}" STREQUAL "${output}")
        message(FATAL_ERROR "Paper results must be deterministic")
    endif()
    set(previous "${output}")
endforeach()

set(header "tickforge-orders,1,BTCUSDT\naccount,100000000000,0\nconfig,1000,1000,1000000,1000000,5000000000\n")
set(scratch "${CMAKE_CURRENT_BINARY_DIR}/paper-invalid-scenario.csv")
foreach(rows IN ITEMS "order,999,1,buy,1000,100000000\n"
        "order,2,1,buy,1000,100000000\norder,1,2,sell,1000,0\n"
        "order,1,1,buy,1000,100000000\norder,2,1,sell,1000,0\n"
        "order,1,1,unknown,1000,100000000\n"
        "order,1,1,buy,1.5,100000000\n"
        "order,1,1,buy,9223372036854775808,100000000\n")
    file(WRITE "${scratch}" "${header}${rows}")
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${scratch}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "1" OR "${error}" STREQUAL "" OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Invalid/untriggered scenario must fail: ${rows}")
    endif()
endforeach()

file(READ "${SCENARIO}" scenario_text)
string(REPLACE "\r\n" "\n" scenario_text "${scenario_text}")
string(REPLACE "\n" "\r\n" scenario_text "${scenario_text}")
file(WRITE "${scratch}" "${scenario_text}")
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "fees=0.78001350 USDT")
    message(FATAL_ERROR "CRLF scenario must be supported")
endif()

set(incomplete "${CMAKE_CURRENT_BINARY_DIR}/paper-incomplete-market.csv")
file(WRITE "${scratch}" "${header}")
file(WRITE "${incomplete}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nreset,1,0,0,0,connecting\n")
execute_process(COMMAND "${PROGRAM}" paper "${incomplete}" "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
    message(FATAL_ERROR "Incomplete market session must not produce a completed experiment")
endif()
file(REMOVE "${scratch}" "${incomplete}")
