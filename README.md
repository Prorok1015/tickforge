# tickforge

A C++ trading research engine for Linux with live market data, paper trading, and deterministic replay.

## Status

**Planning stage.** This repository currently contains project documentation and an Apache 2.0 license. The capabilities below describe the intended design and roadmap; no runnable trading engine is available yet.

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

The first venue, instrument, C++ standard, dependencies, and build tooling are still to be selected.

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

All milestones below are planned.

### 1. Market data capture

- [ ] Select one venue and instrument.
- [ ] Establish a Linux C++ build and basic checks.
- [ ] Connect to the public market data feed.
- [ ] Record events with timestamps and ordering metadata.
- [ ] Detect disconnects and sequence gaps; implement recovery.

### 2. Deterministic replay

- [ ] Replay a recorded session through the event pipeline.
- [ ] Introduce a simulation clock.
- [ ] Verify reproducible event ordering and results.

### 3. First complete paper trading experiment

- [ ] Build and validate a local order book.
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

There is no executable or build procedure yet. Build instructions, dependencies, example configurations, and a reproducible sample session will be added with the first implementation.

## License

Licensed under the [Apache License, Version 2.0](LICENSE).

