# First implementation steps

## Tooling decisions

Use C++20 without compiler extensions, CMake 3.20 or newer, and CTest.
C++20 provides useful standard facilities while remaining available on established
Linux toolchains; this project does not need C++23 features yet. Core events, replay,
recording, and order book code form `tickforge_core`. Console presentation has its
own library, and the optional Binance adapter owns its network dependencies. Each
production target has a matching directory and local `CMakeLists.txt`; see the
[code map](code-map.md).
Ninja is a convenient optional build backend. Start without downloaded dependencies.

Console text is assembled with `std::format` and written with checked stdio calls;
this retains C++20 rather than requiring C++23 `std::print`. CMake verifies that the
compiler's standard library implements `<format>` before configuring the targets.

CTest runs CLI smoke, replay, and order book checks, including malformed input,
ordering, and recovery. Tests use small dependency-free executables with explicit
checks that also run in Release mode. Consider Catch2 as test complexity grows.
Do not depend on live APIs in tests.

## Small milestones

1. **Build scaffold (implemented):** CLI, CMake, and startup smoke check.
2. **Trade event contract and synthetic replay (implemented):** checked integer price/quantity units,
   explicit scales and rounding, exchange and receive timestamps, monotonic receive
   intervals, local event ordinal, file version, and deterministic offline fixture.
   Tests cover malformed input, ordering, and identical replay output. See
   [session formats](session-format.md).
3. **Local order book (implemented):** v2 snapshots and depth ranges, absolute-size
   updates, deletion, best quotes/spread, invalidation, and recovery tests using
   synthetic venue-shaped fixtures. Human-readable CLI shows per-event book state.
4. **Public depth capture (implemented):** optional Boost.Beast/OpenSSL/nlohmann-json
   adapter for Binance Spot BTCUSDT, normalized v3 recording, snapshot bridging,
   sequencing, stale-depth detection, bounded reconnects, and fresh snapshots.
   Receipt order is preserved through capture/replay. Trade capture is still planned.
   See [live capture](live-capture.md).
5. **First paper experiment (partially implemented):** scripted market requests,
   execution interface/simulator, depth consumption, partial fills, explicit fees,
   a separate cash/BTC/reservation ledger, order/position limits, stale-data stop,
   and a console journal. See [paper trading](paper-trading.md). Automatic strategy,
   cost basis/PnL, daily-loss limits, and machine-readable journal export remain planned.
6. **Reproducible Linux delivery:** CI, synthetic end-to-end example, then profiling.

The remaining parts of step 5 and step 6 are planned. Each step should finish with relevant tests and updated
documentation before the next one begins. No real execution adapter is included.

## Selected venue

Binance Spot BTCUSDT was selected with the user on 2026-10-24 because its documented diff
depth stream and REST snapshot synchronization procedure expose update-ID ranges
that make gap handling an explicit, testable exercise. Its market-data-only REST
and WebSocket hosts require no API key and cannot access user streams. This is an
engineering choice, not a recommendation to trade or a claim of HFT latency.

Official references checked on 2026-10-24:

- [Market-data-only endpoints](https://github.com/binance/binance-spot-api-docs/blob/master/faqs/market_data_only.md)
- [Spot streams and local-book synchronization](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md)
- [Binance terms](https://www.binance.com/en/terms)
- [Alternative: Kraken L2 book](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book)
- [Kraken regional terms](https://www.kraken.com/legal)

The Binance stream documentation describes limited connection lifetime, ping/pong,
message limits, and rebuilding a book after a sequence gap. Snapshot depth is
bounded; unseen deeper levels are unknown. Instrument filters must be obtained
from current exchange metadata, not hardcoded from a sample price.

Kraken BTC/USD is an alternative with subscription snapshots and a book checksum;
its integrity mechanism requires a different adapter design. Neither feed gives
exact queue position for simulated orders.

API documentation confirms public technical access, not permission to redistribute
recorded data. The Binance terms page did not expose substantive terms through the
research browser, so recording/redistribution rights remain unresolved. Regional
eligibility and endpoint reachability from a future deployment remain unverified.
Public REST/WebSocket access and a short local capture were verified in the current
WSL environment. Check applicable provider/API terms for the intended use; before
publishing recordings, establish redistribution permission.
Use synthetic fixtures in the repository. Venue and instrument selection does not
resolve the access and data-use questions above.
