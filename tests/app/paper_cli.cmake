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
            "PARTIAL (remainder cancelled)" "book unavailable or stale" "price=64998.00 USDT"
            "cost basis=390.40093949 USDT | average cost=65066.83 USDT | realized=-0.39757401 USDT"
            "MARK    | bid 65000.00 USDT at event #8"
            "equity=999.20148650 USDT"
            "realized=-0.39757401 USDT | unrealized=-0.40093949 USDT | total=-0.79851350 USDT")
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

foreach(account IN ITEMS "account,100000000000,100000" "account,100000000000,0,1" "account,1,2,3,4")
    file(WRITE "${scratch}" "tickforge-orders,1,BTCUSDT\n${account}\nconfig,1000,1000,1000000,1000000,5000000000\n")
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${scratch}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Initial BTC must have exactly one explicit cost basis: ${account}")
    endif()
endforeach()
file(WRITE "${scratch}" "tickforge-orders,1,BTCUSDT\naccount,0,100000,6500100000\nconfig,1000,1000,1000000,1000000,5000000000\n")
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "unrealized=-0.00100000 USDT")
    message(FATAL_ERROR "Inherited BTC must be marked against its declared cost: ${output} ${error}")
endif()

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

# Machine-readable reports. A comma in the scenario path checks CSV quoting.
set(reports "${CMAKE_CURRENT_BINARY_DIR}/paper-reports")
file(REMOVE_RECURSE "${reports}")
file(MAKE_DIRECTORY "${reports}")
set(comma_scenario "${reports}/paper,scenario.csv")
file(COPY_FILE "${SCENARIO}" "${comma_scenario}")
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${comma_scenario}" "${reports}/run"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/run-orders.csv" orders)
file(READ "${reports}/run-summary.csv" summary)
foreach(expected IN ITEMS
        "4,300,6,script,buy,100000,10000000000,0,rejected,0,100000,0,0,60933448950,600000,39039975974,-26575076,,,none,book unavailable or stale\n"
        "8,700,8,script,sell,100000,0,1,filled,100000,0,1,6500000,60920148650,600000,39040093949,-39757401,700,700,none,filled\n")
    string(FIND "${orders}" "${expected}" position)
    if(NOT "${result}" STREQUAL "0" OR position EQUAL -1)
        message(FATAL_ERROR "Order journal missing: ${expected}\n${orders}\n${error}")
    endif()
endforeach()
foreach(expected IN ITEMS "command,paper\n" "scenario,\"${comma_scenario}\"\n" "strategy,script\n" "script_orders,8\n"
        "requests,8\nrejected,1\nfills,8\nfees_quote_atoms,78001350\n" "realized_pnl_quote_atoms,-39757401\n"
        "mark_price_atoms,6500000\n" "unrealized_pnl_quote_atoms,-40093949\n" "total_pnl_quote_atoms,-79851350\n"
        "equity_quote_atoms,99920148650\n")
    string(FIND "${summary}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Summary missing: ${expected}\n${summary}")
    endif()
endforeach()
file(STRINGS "${reports}/run-fills.csv" fill_rows)
list(LENGTH fill_rows fill_count)
if(NOT fill_count EQUAL 9 OR NOT summary MATCHES "status,complete\n$")
    message(FATAL_ERROR "Fill journal needs a header plus eight fills; summary must end complete")
endif()
# Never overwrite an experiment.
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" "${SCENARIO}" "${reports}/run"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/run-summary.csv" unchanged)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "already exists" OR NOT "${unchanged}" STREQUAL "${summary}")
    message(FATAL_ERROR "Existing reports must be preserved: ${error}")
endif()
# A failed experiment leaves journals but no summary.
execute_process(COMMAND "${PROGRAM}" paper "${incomplete}" "${scratch}" "${reports}/failed"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "1" OR NOT EXISTS "${reports}/failed-orders.csv" OR EXISTS "${reports}/failed-summary.csv")
    message(FATAL_ERROR "A failed run must not write a summary")
endif()
file(REMOVE_RECURSE "${reports}")
file(REMOVE "${scratch}" "${incomplete}")
