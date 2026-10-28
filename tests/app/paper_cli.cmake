set(previous "")
foreach(iteration RANGE 1 2)
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${SCENARIO}"
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
            "realized=-0.39757401 USDT | unrealized=-0.40093949 USDT | total=-0.79851350 USDT"
            "Overrides | none")
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

# Command-line shape: a missing config, an unknown flag, or a flag without a value is a
# usage error (exit 2), distinct from a failed experiment (exit 1).
foreach(arguments IN ITEMS "paper;${SESSION}" "paper;${SESSION};--config" "paper;${SESSION};--config;${SCENARIO};--bogus;1"
        "paper;--config;${SCENARIO}" "paper;${SESSION};--config;${SCENARIO};--config;${SCENARIO}")
    execute_process(COMMAND "${PROGRAM}" ${arguments} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "2" OR NOT error MATCHES "Usage")
        message(FATAL_ERROR "Malformed command line must print usage: ${arguments}")
    endif()
endforeach()

set(base "version = 1\n[account]\nusdt = \"1000\"\n[execution]\nfee = \"0.1%\"\nquantity_step = \"0.00001\"\nmax_order = \"0.01\"\nmax_position = \"0.01\"\nstale_after = \"5s\"\n")
set(buy "[[orders]]\nafter_event = 1\nid = 1\nside = \"buy\"\nquantity = \"0.00001\"\nmax_spend = \"1\"\n")
set(scratch "${CMAKE_CURRENT_BINARY_DIR}/paper-invalid-config.toml")
# Each invalid config fails with a message naming the offending key.
foreach(case IN ITEMS
        "orders[0].after_event|[[orders]]\nafter_event = 0\nid = 1\nside = \"buy\"\nquantity = \"0.00001\"\nmax_spend = \"1\"\n"
        "orders[1].after_event|${buy}[[orders]]\nafter_event = 0\nid = 2\nside = \"sell\"\nquantity = \"0.00001\"\n"
        "orders[1].id|${buy}${buy}"
        "orders[0].side|[[orders]]\nafter_event = 1\nid = 1\nside = \"hold\"\nquantity = \"0.00001\"\n"
        "orders[0].quantity: write decimal values as strings|[[orders]]\nafter_event = 1\nid = 1\nside = \"sell\"\nquantity = 0.5\n"
        "orders[0].quantity: '0.000000001' has more than 8|[[orders]]\nafter_event = 1\nid = 1\nside = \"sell\"\nquantity = \"0.000000001\"\n"
        "orders[0].quantity: '92233720368.54775808' is too large|[[orders]]\nafter_event = 1\nid = 1\nside = \"sell\"\nquantity = \"92233720368.54775808\"\n"
        "orders[0].prise: unknown key|[[orders]]\nafter_event = 1\nid = 1\nside = \"sell\"\nquantity = \"0.00001\"\nprise = \"1\"\n"
        "orders[0].max_spend: applies to buys only|[[orders]]\nafter_event = 1\nid = 1\nside = \"sell\"\nquantity = \"0.00001\"\nmax_spend = \"1\"\n"
        "orders[0].max_spend: is required|[[orders]]\nafter_event = 1\nid = 1\nside = \"buy\"\nquantity = \"0.00001\"\n"
        "use either|[strategy]\ntype = \"threshold\"\n${buy}"
        "line 10|usdt = \n")
    string(FIND "${case}" "|" bar)
    string(SUBSTRING "${case}" 0 ${bar} expected)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${case}" ${start} -1 rows)
    file(WRITE "${scratch}" "${base}${rows}")
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${scratch}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    string(FIND "${error}" "${expected}" position)
    if(NOT "${result}" STREQUAL "1" OR position EQUAL -1 OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Invalid config must fail naming '${expected}':\n${rows}\n${error}")
    endif()
endforeach()
file(WRITE "${scratch}" "${base}[[orders]]\nafter_event = 999\nid = 1\nside = \"sell\"\nquantity = \"0.00001\"\n")
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
    message(FATAL_ERROR "An order after the last event must fail the experiment")
endif()

# Overrides are validated like file values; only scalars inside tables can be replaced.
file(WRITE "${scratch}" "${base}")
foreach(case IN ITEMS "account.btc_cost: is required with initial BTC|account.btc=0.001"
        "account.btc_cost: requires initial BTC|account.btc_cost=1"
        "execution.fee: '0.1' must end with %|execution.fee=0.1"
        "execution.stale_after: '5' needs a unit|execution.stale_after=5"
        "execution: fee must be at most 100%|execution.fee=101%"
        "execution.fees: unknown key|execution.fees=0.1%"
        "only single values|execution=1" "lowercase names|Execution.fee=0.1%" "key=value|execution.fee")
    string(FIND "${case}" "|" bar)
    string(SUBSTRING "${case}" 0 ${bar} expected)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${case}" ${start} -1 assignment)
    execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${scratch}" --set "${assignment}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    string(FIND "${error}" "${expected}" position)
    if(NOT "${result}" STREQUAL "1" OR position EQUAL -1)
        message(FATAL_ERROR "Override '${assignment}' must fail naming '${expected}': ${error}")
    endif()
endforeach()
# Later overrides win; inherited BTC is marked against its declared cost.
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${scratch}" --set account.usdt=5
    --set account.usdt=0 --set account.btc=0.001 --set account.btc_cost=65.001
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "unrealized=-0.00100000 USDT"
        OR NOT output MATCHES "Initial   \\| 0.00000000 USDT \\| 0.00100000 BTC \\| BTC cost basis=65.00100000 USDT")
    message(FATAL_ERROR "Inherited BTC must be marked against its declared cost: ${output} ${error}")
endif()

file(READ "${SCENARIO}" scenario_text)
string(REPLACE "\r\n" "\n" scenario_text "${scenario_text}")
# file(WRITE) already translates newlines on Windows; request CRLF explicitly instead.
file(CONFIGURE OUTPUT "${scratch}" CONTENT "${scenario_text}" @ONLY NEWLINE_STYLE CRLF)
file(READ "${scratch}" written HEX)
if(NOT written MATCHES "0d0a" OR written MATCHES "0d0d")
    message(FATAL_ERROR "CRLF fixture was not written as single CRLF line endings")
endif()
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "fees=0.78001350 USDT")
    message(FATAL_ERROR "CRLF config must be supported")
endif()

set(incomplete "${CMAKE_CURRENT_BINARY_DIR}/paper-incomplete-market.csv")
file(WRITE "${scratch}" "${base}")
file(WRITE "${incomplete}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nreset,1,0,0,0,connecting\n")
execute_process(COMMAND "${PROGRAM}" paper "${incomplete}" --config "${scratch}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
    message(FATAL_ERROR "Incomplete market session must not produce a completed experiment")
endif()

# Machine-readable reports. A comma in the config path checks CSV quoting.
set(reports "${CMAKE_CURRENT_BINARY_DIR}/paper-reports")
file(REMOVE_RECURSE "${reports}")
file(MAKE_DIRECTORY "${reports}")
set(comma_config "${reports}/paper,config.toml")
file(COPY_FILE "${SCENARIO}" "${comma_config}")
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${comma_config}" --report "${reports}/run"
    --set execution.stale_after=5s
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
foreach(expected IN ITEMS "version,3\n" "command,paper\n" "config,\"${comma_config}\"\n"
        "overrides,execution.stale_after=5s\n" "fee_ppm,1000\n" "stale_after_ns,5000000000\n"
        "strategy,script\n" "script_orders,8\n"
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
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${SCENARIO}" --report "${reports}/run"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/run-summary.csv" unchanged)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "already exists" OR NOT "${unchanged}" STREQUAL "${summary}")
    message(FATAL_ERROR "Existing reports must be preserved: ${error}")
endif()
# A failed experiment leaves journals but no summary.
execute_process(COMMAND "${PROGRAM}" paper "${incomplete}" --config "${scratch}" --report "${reports}/failed"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "1" OR NOT EXISTS "${reports}/failed-orders.csv" OR EXISTS "${reports}/failed-summary.csv")
    message(FATAL_ERROR "A failed run must not write a summary")
endif()
file(REMOVE_RECURSE "${reports}")
file(REMOVE "${scratch}" "${incomplete}")
