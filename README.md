# tickforge

A C++ trading research engine for Linux with live market data, paper trading, and deterministic replay.

## Status

**Offline strategy experiments, paper trading, and public depth capture.** The C++20 CLI replays market sessions and executes scripted or strategy-generated virtual market orders against known depth. A separate ledger tracks USDT/BTC totals, reservations, fills, fees, average cost basis, and realized/unrealized PnL. Optional Binance public capture records depth and trades in one session and recovers a local book. Real execution remains planned. See the [automatic strategy example](docs/strategies.md).

## Why tickforge?

tickforge is a hands-on project for exploring market microstructure, testing trading hypotheses, and learning Linux systems programming through a working trading simulator.

The initial goal is to consume real market data while trading entirely with virtual funds. Recorded sessions will make experiments repeatable and allow execution assumptions to be examined rather than hidden behind a backtest result.

The project also serves as an engineering portfolio: correctness, recovery from failures, reproducible experiments, and measured performance are central goals.

## Initial scope

Start with one venue, one instrument, and a command-line application.

Target capabilities (see the checked roadmap for implemented parts):

- Receive live quotes, trades, and order book updates through a public market data feed.
- Record market events and replay sessions offline.
- Maintain a local order book and detect gaps in the incoming feed.
- Run strategies against live or replayed events through the same strategy interface.
- Simulate orders, fills, fees, positions, and profit and loss (PnL).
- Produce an execution journal and a session summary.
- Measure processing latency and investigate bottlenecks on Linux.

The default build uses C++20, CMake, and CTest without system libraries; TOML experiment configs are read with the vendored single-header toml++ (`third_party/`). Optional public capture uses Boost.Beast, OpenSSL, and nlohmann/json. Binance Spot BTCUSDT is selected for the first adapter. See [first implementation steps](docs/first-steps.md) for rationale, alternatives, and data-access limitations.

See the [code map](docs/code-map.md) for target directories, file responsibilities,
and dependency/data-flow diagrams. Each production target owns a directory and a
local `CMakeLists.txt`; `main.cpp` delegates to the application command dispatcher.
Venue-specific market data code lives under `src/adapters/<venue>/`; the first
adapter is `adapters/binance` (`tickforge_binance`). Replay uses the same event
model and remains in `core`. Paper execution and strategies are separate components.
For Visual Studio builds and launches targeting Linux, see [Visual Studio + WSL](docs/visual-studio-wsl.md).

## Intended architecture

```text
Live feed ──> Market data adapter ──> Event stream ──> Order book
                                         │               │
                                         v               v
                                     Recorder         Strategy
                                         │               │
                                         v               v
                                    Replay source    Risk checks
                                         │               │
                                         └──> Event      v
                                              stream  Simulated execution
                                                         │
                                                         v
                                                  Portfolio and reports
```

- **Market data adapter:** connection management, parsing, normalization, sequence validation, and recovery.
- **Recorder and replay source:** persistent event history, preserved ordering, and a controllable replay clock.
- **Order book:** local market state built from snapshots and incremental updates.
- **Strategy:** consumes market events and submits order requests without depending on a specific venue connection.
- **Risk checks:** validate requests against configured limits and current system state.
- **Simulated execution:** applies explicit fill, fee, and latency assumptions.
- **Portfolio and reports:** track cash, inventory, realized and unrealized PnL, and execution history.

The design will begin with a simple execution model. Concurrency and additional infrastructure will be introduced when measurements justify them.

## Execution model

Simulation realism will be developed incrementally:

| Stage | Model |
| --- | --- |
| Top of book | Market buys execute against the best ask; market sells execute against the best bid, subject to modeled available size and fees. |
| Order book depth | Orders can consume multiple price levels and receive partial fills. |
| Latency | Submission and cancellation take effect after a modeled delay. |
| Limit orders | Fill estimates account for queue position using documented assumptions supported by the available feed. |

The current [paper command](docs/paper-trading.md) implements immediate market fills
across known depth, partial execution, configurable USDT fees, and virtual liquidity
depletion. Latency and limit-order stages remain planned.

A trade occurring at a limit order's price does not by itself prove that the simulated order would have filled.

Public feeds may not expose enough information to reconstruct queue position precisely. Virtual orders also do not affect the real market. Results must therefore be interpreted within the chosen execution model.

## Engineering principles

- **Reproducibility:** identical recorded inputs, configuration, and random seed, where applicable, should produce identical replay results.
- **No look-ahead:** a strategy may only use information available at the current simulated time.
- **Explicit assumptions:** fees, execution rules, delays, and valuation methods belong in experiment configuration and reports.
- **Correct accounting:** cash, positions, fills, and PnL must remain consistent across partial fills and cancellations.
- **Recovery:** missing or stale data must be detected; an invalid book must not silently drive trading decisions.
- **Measurement before optimization:** investigate latency distributions and resource use before changing the architecture.
- **Separation of concerns:** strategies, market data, execution, and accounting have distinct interfaces.

Exchange timestamps and local receive timestamps will be recorded separately. Internal elapsed-time measurements will use a monotonic clock.

## Roadmap

Checked items below are implemented; unchecked items remain planned.

### 1. Market data capture

- [x] Select one venue and instrument.
- [x] Establish a Linux C++ build and basic checks.
- [x] Connect to the public depth feed.
- [x] Record public trades with ID and aggressor side on the same stream.
- [x] Record depth and lifecycle events with timestamps and ordering metadata.
- [x] Detect disconnects and sequence gaps; implement bounded recovery.

### 2. Deterministic replay

- [x] Replay a versioned synthetic trade session through a synchronous event handler.
- [x] Replay synthetic snapshots and depth updates with deterministic book state.
- [x] Extend replay to captured sessions with depth and lifecycle events.
- [x] Advance execution time using recorded monotonic offsets.
- [x] Verify reproducible event ordering and book results on synthetic sessions.

### 3. First complete paper trading experiment

- [x] Build and validate a local order book using synthetic snapshots and depth updates.
- [x] Add an execution interface and a scripted virtual-order scenario.
- [x] Add a strategy interface and a simple example strategy (offline replay).
- [x] Simulate market orders with spread, available depth, and fees.
- [x] Track total, reserved, and available USDT/BTC in a separate account ledger.
- [x] Add cost basis and realized/unrealized PnL.
- [x] Add position and order-size limits.
- [x] Print an operation journal and final account summary.
- [x] Export a machine-readable trade journal and session summary.

**First milestone of a complete system:** record a market session, replay it, and execute virtual market orders with explicit liquidity and fee assumptions.

### 4. More realistic execution

- [x] Model order submission and report delays.
- [x] Model cancellation delays: cancels travel with the order latency and can lose the race to a fill.
- [x] Make the execution interface asynchronous: orders are submitted, and their
  confirmed terminal results are reported later instead of returned at once.
- [x] Add limit orders, partial fills, and conservative queue assumptions (back of the queue,
  trade-through, crossing book, cancellations behind us), with maker and taker fees.
- [ ] Compare results under different execution assumptions.
- [x] Block decisions on stale or invalid depth.
- [x] Add an automatic trading stop (order-rate halt).
- [x] Add session loss and drawdown limits based on equity (reduce-only).
- [ ] Reset loss limits per calendar day for multi-day runs.
- [ ] Persist ledger state and open orders; restore them after a restart.

The asynchronous lifecycle, risk stops, and persistence are simulation features in
their own right. They are also the parts a later live adapter would depend on, so
the simulator should exercise them first.

### 5. Forward paper trading and a shared session engine

- [ ] Define a market source interface implemented by file replay and the live feed.
- [ ] Extract the per-event loop (source -> risk -> strategy -> order manager ->
  reports) from the app into a reusable `tickforge_engine` target.
- [ ] Add a forward paper mode: the live public feed with simulated execution and
  virtual funds, no keys.
- [ ] Record the live session alongside forward trading so the run can be replayed.
- [ ] Run several strategies on one account: per-strategy sub-ledgers (position, cost,
  PnL) with fills tagged by strategy, per-strategy and account risk limits, capital
  allocation, and self-trade prevention. Until then a run has exactly one strategy.

Paper, forward, testnet, and live runs differ only at the edges: where events come
from and where orders go. The loop between them must be one shared implementation so
that simulated behaviour cannot drift from live behaviour. Modes are combinations of
interchangeable parts selected by the app, not separate copies of the loop. The
extraction waits for the forward mode, its first second consumer, and for limit orders
to settle the shape of the loop.

### 6. Linux performance research

- [ ] Measure processing latency, including percentiles.
- [ ] Profile CPU use, allocations, and system calls.
- [ ] Investigate socket buffering, event-driven I/O, and scheduling.
- [ ] Evaluate concurrency and CPU affinity where justified.

### 7. Optional future live execution

A live execution adapter may be explored after the simulator, accounting, and risk controls have been validated. This is a later research direction; real order submission is outside the initial scope.

The long-term idea is a staged promotion of each strategy, with the same strategy
code at every stage:

1. **Simulation:** recorded and replayed sessions; the strategy must stay profitable
   after fees, slippage, and delays on sessions it was not tuned on.
2. **Exchange testnet:** real order lifecycle and connectivity, no real money.
3. **Real funds:** minimal amounts first.

A web interface could later show sessions and results and switch a strategy between
stages. None of this is planned work yet. Prerequisites beyond sections 4-6 would
include a separate authenticated execution adapter (never part of the public
market-data adapter), client order IDs for safe retries, reconciliation of orders and
balances against the exchange after disconnects, venue order filters and rate limits,
restricted trade-only API keys, and monitoring with alerts.

## Validation plan

Testing will focus on behavior that can materially change results:

- Order book snapshot and update handling.
- Feed gaps, reconnects, and resynchronization.
- Order lifecycle, partial fills, and cancellations.
- Cash, position, fee, and PnL accounting.
- Replay determinism and prevention of look-ahead.
- Risk limits and handling of stale or invalid market data.

Performance results will include the workload, hardware, build configuration, and measurement method so that comparisons are meaningful.

## Getting started

Requirements: a Linux C++20 compiler and standard library with `std::format` support
(for example GCC 13+ with matching libstdc++), CMake 3.20+, and a build tool.
CMake checks `<format>` support explicitly; a compiler accepting C++20 alone is
not sufficient. GCC 15.2 is the currently verified Linux compiler.
For Ubuntu, install the packages if needed:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build
```

From the repository root in a Linux or WSL Ubuntu shell:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/tickforge
```

The startup screen includes:

```text
tickforge | Market research engine
Mode      | SIMULATION ONLY
```

The twelve default CTest checks cover startup, input validation, deterministic console output,
snapshots, absolute updates, deletions, stale ranges, gaps, crossed books, recovery,
decimal display, recording, and live/replay synchronization state equality. Enabling
live capture adds a thirteenth offline test for Binance JSON/metadata normalization.
Paper checks cover fees, reserves, partial fills, duplicate receipts, uncertain
results, order/position limits, stale data, and checked monetary arithmetic.
No network connection or credentials are needed.

Replay the synthetic example:

```sh
./build/tickforge replay examples/synthetic-book.csv
```

The example deliberately includes a gap and recovery: the final summary reports
eight events, two integrity warnings, and a book synchronized at ID 112. The older
trade-only `examples/synthetic.csv` still works. See [session formats](docs/session-format.md)
for units, timestamp semantics, output, and failure behavior. Quotes show only known
recorded levels, not guaranteed complete market depth.

In Visual Studio's `launch.vs.json`, set `"currentDir": "${projectDir}"` and
`"args": ["replay", "examples/synthetic-book.csv"]` to resolve the fixture from
the repository root.

Run the complete virtual-account example (works in native Windows and Linux builds):

```sh
./build/tickforge paper examples/synthetic-book.csv --config examples/paper-orders.toml
```

It starts with 1000 virtual USDT and finishes with **609.20148650 USDT and 0.006 BTC**,
eight fills, one rejected request, and zero outstanding reserves. Marked at the last
trusted bid (65000.00), the result is **-0.79851350 USDT**: -0.39757401 realized and
-0.40093949 unrealized. In Visual Studio, use:

```json
"args": ["paper", "examples/synthetic-book.csv", "--config", "examples/paper-orders.toml"]
```

Add `--report reports/example` to also write machine-readable order, fill, and
summary CSV files, and `--set execution.fee=0.075%` (repeatable) to override a config
value without editing the file. See [paper trading](docs/paper-trading.md)
for the config format, units, fee rounding, report files, liquidity assumptions, and the separate
accounting/execution components.

For a release build, use a separate directory:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

To build and run **public Binance depth capture** under Linux/WSL, follow
[live capture](docs/live-capture.md). It requires `-DTICKFORGE_ENABLE_LIVE=ON` and
the optional dependencies. Example after that build:

```sh
mkdir -p recordings
./build-live/tickforge capture recordings/btcusdt-session.csv 30
./build-live/tickforge replay recordings/btcusdt-session.csv
```

The file must be new. Capture uses no keys and cannot place orders. Successful
shutdown invalidates the book and saves a completion footer; incomplete files are
rejected by replay. Actual market recordings should remain private unless provider
redistribution rights have been established.

Verified on 2026-10-24 in Ubuntu under WSL2 (Linux 6.6.87.2, GCC 15.2.0,
CMake 3.28.3, Ninja). A short real depth capture and offline replay were also verified
in WSL. Native Windows x64 Debug replay is checked using Visual Studio 2026 / MSVC
19.51. Native Windows networking, standalone Linux hosts, other compilers, and
long-running/24-hour network recovery remain unverified. Linux is the target platform.

## License

Licensed under the [Apache License, Version 2.0](LICENSE).

