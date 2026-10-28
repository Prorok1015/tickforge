foreach(iteration RANGE 1 2)
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" --config "${SCENARIO}"
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
execute_process(COMMAND "${PROGRAM}" strategy "${prefix}" --config "${SCENARIO}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "1 requests"
        OR output MATCHES "SELL bid" OR NOT output MATCHES "total=0.00100000"
        OR NOT output MATCHES "unrealized=-0.00110000 USDT")
    message(FATAL_ERROR "Prefix replay must hold the partial buy without a future sell: ${output} ${error}")
endif()
execute_process(COMMAND "${PROGRAM}" paper "${SESSION}" --config "${SCENARIO}" RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "Script command must reject strategy configuration")
endif()
# Stop-loss: buy at 100.00, the bid drops to 98.00, the position is sold at a loss.
# The policy is flat again, so the next event rebuys while ask <= 100.00 (no cooldown).
set(falling "${CMAKE_CURRENT_BINARY_DIR}/strategy-falling.csv")
file(WRITE "${falling}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nsnapshot,1,0,1000000100,0,1,1,1,9900,1000000,10000,100000\nsnapshot,2,0,1000000200,100,2,1,1,9800,1000000,9900,1000000\ntrade,3,1000000350,1000000400,200,9900,1000\nend,3\n")
execute_process(COMMAND "${PROGRAM}" strategy "${falling}" --config "${SCENARIO}" --set strategy.stop_loss=1%
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
execute_process(COMMAND "${PROGRAM}" strategy "${falling}" --config "${SCENARIO}"
    --set strategy.stop_loss=1% --set strategy.cooldown=1000ns
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
string(FIND "${output}" "Cooldown  | event #3 | stop-loss closed the position; no buys until elapsed=1200 ns" cooldown)
if(NOT "${result}" STREQUAL "0" OR cooldown EQUAL -1 OR output MATCHES "event #3 \\| BUY"
        OR NOT output MATCHES "Paper complete \\| 2 requests")
    message(FATAL_ERROR "Cooldown must block the rebuy after a stop-loss: ${output} ${error}")
endif()
# Loss limit: at event #2 equity is 99.8999 USDT + 0.001 BTC at 98.00 = 99.9979 USDT,
# 0.0021 below the start, so reduce-only begins. The stop-loss sell is still allowed,
# and the immediate rebuy at event #3 is blocked by the account, not by a cooldown.
execute_process(COMMAND "${PROGRAM}" strategy "${falling}" --config "${SCENARIO}"
    --set strategy.stop_loss=1% --set risk.max_loss=0.002
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
foreach(expected IN ITEMS "Risk      | max loss=0.00200000 USDT | max drawdown=off"
        "Risk      | event #2 | REDUCE-ONLY: sells only after max_loss | equity=99.99790000 USDT | start=100.00000000 USDT"
        "Signal    | event #2 | STOP-LOSS" "Paper complete | 2 requests | 0 rejected | 2 fills")
    string(FIND "${output}" "${expected}" position)
    if(NOT "${result}" STREQUAL "0" OR position EQUAL -1 OR output MATCHES "event #3 \\| BUY")
        message(FATAL_ERROR "Loss limit behaviour missing: ${expected}\n${output}\n${error}")
    endif()
endforeach()
file(REMOVE "${falling}")

# Invalid strategy values are rejected naming the key.
foreach(case IN ITEMS "strategy: invalid threshold|strategy.sell_above=99"
        "strategy: invalid threshold|strategy.quantity=0.000001"
        "strategy: invalid threshold|strategy.buy_budget=0"
        "strategy: invalid threshold|strategy.stop_loss=100%"
        "strategy: invalid threshold|strategy.cooldown=5ns"
        "strategy.type: only|strategy.type=grid"
        "strategy.buy_below: '100.001' has more than 2|strategy.buy_below=100.001")
    string(FIND "${case}" "|" bar)
    string(SUBSTRING "${case}" 0 ${bar} expected)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${case}" ${start} -1 assignment)
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" --config "${SCENARIO}" --set "${assignment}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    string(FIND "${error}" "${expected}" position)
    if(NOT "${result}" STREQUAL "1" OR position EQUAL -1 OR output MATCHES "Paper complete")
        message(FATAL_ERROR "Invalid strategy override '${assignment}' must fail naming '${expected}': ${error}")
    endif()
endforeach()

# Strategy reports carry signals and parameters, and are identical across runs.
set(reports "${CMAKE_CURRENT_BINARY_DIR}/strategy-reports")
file(REMOVE_RECURSE "${reports}")
file(MAKE_DIRECTORY "${reports}")
foreach(run IN ITEMS first second)
    execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" --config "${SCENARIO}" --report "${reports}/${run}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0" OR NOT output MATCHES "Report    \\| ")
        message(FATAL_ERROR "Strategy report failed: ${error}")
    endif()
endforeach()
file(READ "${reports}/first-orders.csv" orders)
file(READ "${reports}/first-summary.csv" summary)
foreach(expected IN ITEMS "1,0,1,buy,buy,market,,0,200000,1000000000,1,partially_filled,100000,100000,1,10000,"
        "3,200,2,take_profit,sell,market,,0,100000,0,1,filled,100000,0,1,10200,10000179800,0,0,179800,200,200,none,filled\n")
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
# Latency: the buy decided at event #1 (t=0) executes at t=150, after the reset at t=100,
# so it is cancelled for lack of a trusted book and counted, not silently dropped.
execute_process(COMMAND "${PROGRAM}" strategy "${SESSION}" --config "${SCENARIO}" --report "${reports}/delayed"
    --set execution.order_latency=150ns
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/delayed-summary.csv" summary)
file(READ "${reports}/delayed-orders.csv" orders)
foreach(expected IN ITEMS "order_latency_ns,150\nreport_latency_ns,0\n" "requests,1\nrejected,0\nfills,0\n"
        "cancelled_no_trusted_book,1\ncancelled_session_ended,0\n" "realized_pnl_quote_atoms,0\n")
    string(FIND "${summary}" "${expected}" position)
    if(NOT "${result}" STREQUAL "0" OR position EQUAL -1)
        message(FATAL_ERROR "Delayed summary missing: ${expected}\n${summary}\n${output}\n${error}")
    endif()
endforeach()
if(NOT orders MATCHES ",150,150,no_trusted_book,no trusted book at execution time; order cancelled\n"
        OR NOT output MATCHES "Sent      \\| order #1 \\| elapsed=0 ns")
    message(FATAL_ERROR "Cancelled in-flight order must be journaled: ${orders}\n${output}")
endif()
# Holding BTC without any trusted bid: unrealized PnL is unavailable, not guessed.
set(no_quotes "${reports}/no-quotes.csv")
file(WRITE "${no_quotes}" "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\nreset,1,0,0,0,connecting\nend,1\n")
execute_process(COMMAND "${PROGRAM}" strategy "${no_quotes}" --config "${SCENARIO}" --report "${reports}/held"
    --set account.usdt=0 --set account.btc=0.001 --set account.btc_cost=0.1
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${reports}/held-summary.csv" summary)
if(NOT "${result}" STREQUAL "0" OR NOT summary MATCHES "mark_price_atoms,\nmark_event_ordinal,\nmarket_value_quote_atoms,\nunrealized_pnl_quote_atoms,\ntotal_pnl_quote_atoms,\nequity_quote_atoms,\n")
    message(FATAL_ERROR "Unavailable valuation must be empty fields: ${summary} ${error}")
endif()
file(REMOVE_RECURSE "${reports}")
