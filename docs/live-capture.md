# Public Binance depth and trade capture

This milestone captures **BTCUSDT spot depth and trades** through public
market-data-only hosts. It does not authenticate, accept keys, or submit orders. The resulting normalized session is readable by the same offline replay
command. Market records are not included in the repository.

The adapter lives in `src/adapters/binance/`, builds as `tickforge_binance`, and
uses the namespace `tickforge::adapters::binance`. `TICKFORGE_ENABLE_LIVE` is kept
as the existing opt-in switch; capture/replay commands are unchanged. This is a
market-data adapter only, not an order-execution API.

## Forward paper trading

`live` runs a strategy on the same public stream, with simulated execution and
virtual funds. No order leaves the machine; there are still no keys.

```sh
./build-live/tickforge live --config FILE.toml [--set KEY=VALUE]... [--duration SECONDS] \
    [--record NEW_SESSION] [--report PREFIX] [--plain]
```

- The config must have a `[strategy]` section; scripted `[[orders]]` need a recording.
- `--duration` is 1..86400 seconds of wall-clock time (default 600), including
  connection. Ctrl+C or SIGTERM stops earlier, the same way: the feed ends with a
  `shutdown` reset, resting and in-flight orders end as `session_ended`, fills that
  already executed are still reported, an open position is valued at the last trusted
  bid (not sold), the recording gets its footer, and the reports are completed.
- `--record` writes the session exactly as `capture` does. Every event is recorded
  before the engine sees it, so the recording holds precisely what the strategy saw.
- `--report` writes the same CSV reports as offline runs, with `command,live`.
- In a terminal the run is a full-screen view redrawn in place, about five times per
  second of session time. Its panels answer, from top to bottom, the questions a
  person watching asks:
  - **RESULT**: how am I doing? Total profit in USDT and percent of the starting
    money, split into closed trades and the open position, plus round trips and fees.
  - **BOT**: what is the bot doing and why? Each open order in plain words: waiting
    to buy or sell at a price, our estimated place in the queue when we are the best
    price, or how far the market has to move to reach an order that it moved away
    from. Then the strategy's rules.
  - **MARKET**: five price levels per side with size bars, our resting orders marked
    beside their level, and a note for our orders outside the shown levels.
  - **WALLET**: USDT and BTC with the free and the locked (reserved) parts, and the
    average purchase price while holding BTC.
  - **ACTIVITY**: the latest events as sentences: placed, bought, sold (and whether
    our order was hit or we took the price), cancels, risk stops, and feed status.

  The final summary is printed after the view closes. `--plain`, or output that is
  not a terminal, prints the journal lines instead, without a line per event.

**Replay equals live.** The engine reads no clock: decisions, latency, and risk use
only the receive offsets stored in the events. Replaying the recording with the same
config and overrides (the command is printed at the end) must reproduce the orders,
fills, markouts, and PnL of the live run byte for byte; only the command and file
names in the summary differ. A difference would mean that something outside the
events influenced a decision. The same recording can then be replayed with other
strategies to compare them on exactly the same market.

During a feed outage, resting orders stay, as they would at a venue. New orders are
cancelled without a trusted book, and after resynchronization resting orders move to
the back of the visible queue; see the paper trading model for details.

## Linux / WSL build

Keep the default dependency-free build for replay, or enable the network component:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build ca-certificates libboost-dev libssl-dev nlohmann-json3-dev
cmake -S . -B build-live -G Ninja -DCMAKE_BUILD_TYPE=Release -DTICKFORGE_ENABLE_LIVE=ON
cmake --build build-live -j 2
ctest --test-dir build-live --output-on-failure
mkdir -p recordings
./build-live/tickforge capture recordings/btcusdt-session.csv 30
./build-live/tickforge replay recordings/btcusdt-session.csv
```

Use a C++20 compiler and standard library supporting `std::format` (the verified
compiler is GCC 15.2). The network component needs Boost 1.74+, OpenSSL, and
nlohmann/json 3.10+. Package names above are for Ubuntu. Boost.Beast handles HTTPS
and WebSocket framing/control messages; OpenSSL verifies certificates and hostnames;
nlohmann/json parses payloads. Decimal strings are converted to checked integers,
without binary floating point. There is one application event loop; asynchronous
DNS resolution may use a library-managed worker.

The output file must not already exist. Recording opens it exclusively and flushes
each event; this favors debuggability and explicit failure over capture throughput.
Create the parent directory beforehand. `recordings/` is ignored by Git. Local
recording does not establish redistribution rights: check applicable provider terms
before publishing any actual market data.

The optional duration defaults to 30 seconds, including setup, and accepts 1..3600.
Ctrl+C/SIGTERM requests an orderly end. A successful recording requires that a
synchronized book was established at least once. A fatal error leaves an incomplete
file without a footer; replay returns failure even if some rows could be processed.
Existing recordings are never overwritten.

For Visual Studio on Windows, use the WSL Linux target with these dependencies and
`TICKFORGE_ENABLE_LIVE=ON`. Arguments are `capture`, `recordings/btcusdt-session.csv`,
`30`; set the working directory to the repository root and create `recordings` there.
Native Windows replay remains supported. Native Windows networking has not been
verified; it also needs the optional dependencies and a usable TLS certificate store.

## Synchronization and recording order

1. Fetch current BTCUSDT `exchangeInfo` and check the tick/quantity step against the
   fixed integer representation. Values or metadata that cannot be represented
   exactly are rejected. This is validation of units/lattice, not order eligibility:
   order-size limits, minimum notional, and execution fees are not modeled yet.
2. Open one combined stream,
   `wss://data-stream.binance.vision/stream?streams=btcusdt@depth@100ms/btcusdt@trade`.
   Each message names its stream; depth and trades are recorded in the single order
   in which this connection delivered them, so replay sees a trade and the depth
   change it caused in receipt order. One connection also means one reconnect path
   and one shared reset. Individual `@trade` events are used rather than `@aggTrade`:
   each carries a consecutive ID, so missing trades are detectable, at the cost of
   more messages (tens per second for BTCUSDT).
3. On the first depth event, request a REST snapshot with up to 5000 levels per side.
   Continue reading, recording, and buffering WebSocket events while REST is pending.
4. Record the snapshot when received. The shared `BookTracker` combines it with
   buffered events and waits for a depth range covering the next required ID.
   Quotes remain hidden until this bridge is established. If the snapshot is too
   old, request a new one on a subsequent depth event.
   Trades are recorded as they arrive (version 4 rows with ID and aggressor). They do
   not touch the book and do not count as depth activity: the five-second staleness
   check uses depth messages only, because a quiet market can legitimately have no
   trades and a live trade stream does not prove that depth updates still arrive.
5. Apply later ranges, ignore covered IDs, and invalidate on a sequence gap. Fetch
   a fresh snapshot while continuing to receive events.

The recording preserves application receipt order across both sources. Its ordinal
is not a Binance ID. Receive timestamps are sampled at the callback for a complete
WebSocket message / HTTP response, before JSON parsing; they are not kernel/NIC
timestamps. Exchange timestamps are converted from the stream's default milliseconds
to nanoseconds without claiming increased precision. REST snapshots and local reset
events have no exchange timestamp and use zero. The monotonic session origin stays
the same across reconnects.

Only known price levels are maintained. A bounded REST snapshot does not reveal all
market depth, and newly changed levels can make the local level count exceed 5000.
Neither a fresh message nor synchronization proves that public data has no latency.

## Failure behavior

- A connection error, server shutdown notification, or more than five seconds without
  a depth message clears quotes immediately when detected and records a local reset.
  The watchdog checks once a second. Reconnection always starts with a fresh snapshot.
- Reconnection delays are 1, 2, 4, 8, and 8 seconds, with at most five reconnects per
  capture. Snapshot requests are limited to twelve per capture. Bootstrap buffering
  is bounded to 4096 events and 200000 price-level changes.
- HTTP errors stop capture and report status / `Retry-After`. In particular, rate
  limits and regional/access denials do not trigger an automatic request loop.
- TLS verification is always enabled. Malformed JSON, invalid instrument values,
  buffer overflow, and storage errors stop capture. Ping/pong frames are handled by
  Beast while reads are active. REST bodies and WebSocket messages have size limits.
- Successful shutdown records `reset,...,shutdown`, then an `end` footer. The final
  replay book is deliberately unavailable after shutdown. A completed file may
  contain intervals of unavailable data; it is not a claim of uninterrupted capture.

The console prints book state about once a second, plus lifecycle/synchronization
changes. Every accepted depth message is still recorded. Replay prints every event.
Slow storage/console output can stall this simple event loop; performance profiling
and an independent receive queue are future work if measurements require them.

## Validation and limits

Automated offline tests cover Binance-shaped JSON, exact decimal normalization,
metadata filters, timestamp/ID overflow, snapshot bridging, gaps, local resets,
recording/replay state equality, exclusive file creation, and incomplete sessions.
No CTest test requires the network. The original synthetic v1/v2 fixtures remain
supported. A short real WSL capture and its offline replay were checked on 2026-10-24;
24-hour rotation and injected network/TLS/rate-limit failures have not been exercised
against the real service. Native Windows networking remains unverified.

The verified Release build with GCC 15.2 and Ubuntu Boost 1.83 emitted an
`-Warray-bounds` diagnostic inside Boost.Asio's resolver implementation. No warnings
were suppressed; the offline and real-network checks completed successfully. This
dependency/compiler diagnostic has not been resolved across other Boost versions.

Official references checked on 2026-10-24:

- [Public market-data-only endpoints](https://github.com/binance/binance-spot-api-docs/blob/master/faqs/market_data_only.md)
- [Depth stream, ping/pong, and local book synchronization](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md)
- [REST exchange metadata and depth snapshot](https://github.com/binance/binance-spot-api-docs/blob/master/rest-api.md)
