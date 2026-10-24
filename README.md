# tickforge

A C++ trading research engine for Linux with live market data, paper trading, and deterministic replay.

## Status

**Public depth capture and deterministic replay.** The C++20 CLI optionally connects to Binance Spot BTCUSDT public depth, synchronizes a local book, and records normalized sessions for offline replay. Gaps, reconnects, stale depth, and snapshot bridging invalidate/hide quotes when needed. Strategy, execution, accounting, and live trade capture remain planned.

## Why tickforge?

tickforge is a hands-on project for exploring market microstructure, testing trading hypotheses, and learning Linux systems programming through a working trading simulator.

The initial goal is to consume real market data while trading entirely with virtual funds. Recorded sessions will make experiments repeatable and allow execution assumptions to be examined rather than hidden behind a backtest result.

The project also serves as an engineering portfolio: correctness, recovery from failures, reproducible experiments, and measured performance are central goals.

## Initial scope

Start with one venue, one instrument, and a command-line application.

Planned capabilities:

- Receive live quotes, trades, and order book updates through a public market data feed.
- Record market events and replay sessions offline.
- Maintain a local order book and detect gaps in the incoming feed.
- Run strategies against live or replayed events through the same strategy interface.
- Simulate orders, fills, fees, positions, and profit and loss (PnL).
- Produce an execution journal and a session summary.
- Measure processing latency and investigate bottlenecks on Linux.

The default replay build uses C++20, CMake, and CTest without external libraries. Optional public capture uses Boost.Beast, OpenSSL, and nlohmann/json. Binance Spot BTCUSDT is selected for the first adapter. See [first implementation steps](docs/first-steps.md) for rationale, alternatives, and data-access limitations.

See the [code map](docs/code-map.md) for target directories, file responsibilities,
and dependency/data-flow diagrams. Each production target owns a directory and a
local `CMakeLists.txt`; `main.cpp` delegates to the application command dispatcher.
Venue-specific market data code lives under `src/adapters/<venue>/`; the first
adapter is `adapters/binance` (`tickforge_binance`). Replay uses the same event
model and remains in `core`. Order execution is a separate, future responsibility.
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
- [x] Connect to the public depth feed (trade stream still planned).
- [x] Record depth and lifecycle events with timestamps and ordering metadata.
- [x] Detect disconnects and sequence gaps; implement bounded recovery.

### 2. Deterministic replay

- [x] Replay a versioned synthetic trade session through a synchronous event handler.
- [x] Replay synthetic snapshots and depth updates with deterministic book state.
- [x] Extend replay to captured sessions with depth and lifecycle events.
- [ ] Introduce a simulation clock.
- [x] Verify reproducible event ordering and book results on synthetic sessions.

### 3. First complete paper trading experiment

- [x] Build and validate a local order book using synthetic snapshots and depth updates.
- [ ] Add a strategy interface and a simple example strategy.
- [ ] Simulate market orders with spread, available depth, and fees.
- [ ] Track cash, positions, and PnL.
- [ ] Add position and order-size limits.
- [ ] Export a trade journal and session summary.

**First milestone of a complete system:** record a market session, replay it, and execute virtual market orders with explicit liquidity and fee assumptions.

### 4. More realistic execution

- [ ] Model order submission and cancellation delays.
- [ ] Add limit orders, partial fills, and queue assumptions.
- [ ] Compare results under different execution assumptions.
- [ ] Add stale-data handling and an emergency trading stop.

### 5. Linux performance research

- [ ] Measure processing latency, including percentiles.
- [ ] Profile CPU use, allocations, and system calls.
- [ ] Investigate socket buffering, event-driven I/O, and scheduling.
- [ ] Evaluate concurrency and CPU affinity where justified.

### 6. Optional future live execution

A live execution adapter may be explored after the simulator, accounting, and risk controls have been validated. This is a later research direction; real order submission is outside the initial scope.

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

The seven default CTest checks cover startup, input validation, deterministic console output,
snapshots, absolute updates, deletions, stale ranges, gaps, crossed books, recovery,
decimal display, recording, and live/replay synchronization state equality. Enabling
live capture adds an eighth offline test for Binance JSON/metadata normalization.
Execution/accounting are not implemented or tested yet.
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

