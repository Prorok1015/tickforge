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
# Stop-loss: buy at 100.00, the bid drops to 98.00, the position is sold at a loss.
# The policy is flat again, so the next event rebuys while ask <= 100.00 (no cooldown).
set(falling "${CMAKE_CURRENT_BINARY_DIR}/strategy-falling.csv")
set(stop_config "${CMAKE_CURRENT_BINARY_DIR}/strategy-stop.csv")
file(WRITE "${falling}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nsnapshot,1,0,1000000100,0,1,1,1,9900,1000000,10000,100000\nsnapshot,2,0,1000000200,100,2,1,1,9800,1000000,9900,1000000\ntrade,3,1000000350,1000000400,200,9900,1000\nend,3\n")
file(WRITE "${stop_config}" "tickforge-orders,1,BTCUSDT\naccount,10000000000,0\nconfig,1000,1000,1000000,1000000,5000000000\nthreshold,10000,10200,200000,1000000000,10000\n")
execute_process(COMMAND "${PROGRAM}" strategy "${falling}" "${stop_config}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
foreach(expected IN ITEMS "Stop-loss | sell at market when position value is 10000 ppm below cost"
        "Signal    | event #2 | STOP-LOSS" "price=98.00 USDT | quantity=0.00100000 BTC"
        "realized=-0.00219800 USDT" "Signal    | event #3 | BUY ask")
    string(FIND "${output}" "${expected}" position)
    if(NOT "${result}" STREQUAL "0" OR position EQUAL -1)
        message(FATAL_ERROR "Missing stop-loss result: ${expected}\n${output} ${error}")
    endif()
endforeach()
# The same session with a 1000 ns cooldown: event #3 (elapsed 200) confirms the exit
# and starts the pause, so the immediate rebuy disappears.
file(WRITE "${stop_config}" "tickforge-orders,1,BTCUSDT\naccount,10000000000,0\nconfig,1000,1000,1000000,1000000,5000000000\nthreshold,10000,10200,200000,1000000000,10000,1000\n")
execute_process(COMMAND "${PROGRAM}" strategy "${falling}" "${stop_config}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
string(FIND "${output}" "Cooldown  | event #3 | stop-loss closed the position; no buys until elapsed=1200 ns" cooldown)
if(NOT "${result}" STREQUAL "0" OR cooldown EQUAL -1 OR output MATCHES "event #3 \\| BUY"
        OR NOT output MATCHES "Paper complete \\| 2 requests")
    message(FATAL_ERROR "Cooldown must block the rebuy after a stop-loss: ${output} ${error}")
endif()
file(REMOVE "${falling}" "${stop_config}")

foreach(row IN ITEMS "threshold,10200,10000,200000,1000000000" "order,1,1,buy,1000,100000"
        "threshold,10000,10200,1,1000000000" "threshold,10000,10200,200000,0"
        "threshold,10000,10200,200000,1000000000,1000000" "threshold,10000,10200,200000,1000000000,0,5"
        "threshold,10000,10200,200000,1000000000,1,2,3")
    string(REGEX REPLACE "threshold,[^\n]+" "${row}" invalid "${config}")
    file(WRITE "${scratch}" "${invalid}")
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" "${scratch}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "1" OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Invalid strategy configuration accepted")
    endif()
endforeach()

# Strategy reports carry signals and parameters, and are identical across runs.
set(reports "${CMAKE_CURRENT_BINARY_DIR}/strategy-reports")
file(REMOVE_RECURSE "${reports}")
file(MAKE_DIRECTORY "${reports}")
foreach(run IN ITEMS first second)
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" "${SCENARIO}" "${reports}/${run}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "Report    \\| ")
        message(FATAL_ERROR "Strategy report failed: ${error}")
    endif()
endforeach()
file(READ "${reports}/first-orders.csv" orders)
file(READ "${reports}/first-summary.csv" summary)
foreach(expected IN ITEMS "1,0,1,buy,buy,200000,1000000000,1,partially_filled,100000,100000,1,10000,"
        "3,200,2,take_profit,sell,100000,0,1,filled,100000,0,1,10200,10000179800,0,0,179800,filled\n")
    string(FIND "${orders}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Strategy order journal missing: ${expected}\n${orders}")
    endif()
endforeach()
foreach(expected IN ITEMS "command,strategy\n" "strategy,threshold\nbuy_price_atoms,10000\nsell_price_atoms,10200\n"
        "stop_loss_ppm,0\ncooldown_ns,0\n" "events,4\n" "realized_pnl_quote_atoms,179800\n"
        "total_pnl_quote_atoms,179800\n" "status,complete\n")
    string(FIND "${summary}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Strategy summary missing: ${expected}\n${summary}")
    endif()
endforeach()
foreach(kind IN ITEMS orders fills)
    file(READ "${reports}/first-${kind}.csv" first)
    file(READ "${reports}/second-${kind}.csv" second)
    if(NOT "${first}" STREQUAL "${second}")
        message(FATAL_ERROR "Strategy ${kind} journal must be deterministic")
    endif()
endforeach()
file(READ "${reports}/second-summary.csv" second)
string(REPLACE "second-" "first-" second "${second}")
if(NOT "${summary}" STREQUAL "${second}")
    message(FATAL_ERROR "Strategy summary must be deterministic")
endif()
# Holding BTC without any trusted bid: unrealized PnL is unavailable, not guessed.
set(no_quotes "${reports}/no-quotes.csv")
set(holding "${reports}/holding.csv")
file(WRITE "${no_quotes}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nreset,1,0,0,0,connecting\nend,1\n")
file(WRITE "${holding}" "tickforge-orders,1,BTCUSDT\naccount,0,100000,10000000\nconfig,1000,1000,1000000,1000000,5000000000\nthreshold,10000,10200,200000,1000000000\n")
execute_process(COMMAND "${PROGRAM}" strategy "${no_quotes}" "${holding}" "${reports}/held"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/held-summary.csv" summary)
if(NOT "${result}" STREQUAL "0" OR NOT summary MATCHES "mark_price_atoms,\nmark_event_ordinal,\nmarket_value_quote_atoms,\nunrealized_pnl_quote_atoms,\ntotal_pnl_quote_atoms,\nequity_quote_atoms,\n")
    message(FATAL_ERROR "Unavailable valuation must be empty fields: ${summary} ${error}")
endif()
file(REMOVE_RECURSE "${reports}")
