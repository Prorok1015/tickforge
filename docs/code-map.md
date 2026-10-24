# Code map

The filesystem follows the production CMake targets. Each target owns a directory
and its own `CMakeLists.txt`; the root only selects options, compiler requirements,
source subdirectories, and tests. Header include paths identify their owner, for
example `<tickforge/core/events.hpp>`.

```text
tickforge/
  CMakeLists.txt                    project options and subdirectories
  CMakePresets.json                 shared Windows / Linux / WSL build profiles
  cmake/ProjectOptions.cmake        compiler capability check and warnings helper
  src/
    CMakeLists.txt                  component build order
    core/                          library: tickforge_core
      CMakeLists.txt
      include/tickforge/core/       public domain/session headers
      src/                         implementations
    console/                       library: tickforge_console
      CMakeLists.txt
      include/tickforge/console/    public presentation headers
      src/                         formatting and replay reporting
    adapters/                      venue adapters, each owning its target
      CMakeLists.txt
      binance/                     optional library: tickforge_binance
        CMakeLists.txt
        include/tickforge/adapters/binance/
        src/                       Binance JSON and TLS/WebSocket implementation
    app/                           executable: tickforge
      CMakeLists.txt
      main.cpp                     entry point
      cli.cpp, cli.hpp             argument dispatch, usage, and exit codes
      commands.hpp                 command declarations
      replay_command.cpp           replay command
      capture_command.cpp          capture command / disabled-feature error
  tests/
    CMakeLists.txt
    core/                          session, synchronization, and book contracts
    console/                       exact decimal display
    adapters/binance/              Binance-shaped JSON (no network)
    app/                           CLI integration checks
```

Public headers describe how another target can use a component. Implementations
live under that component's `src/`. The executable's private headers stay beside
its implementation because no other target consumes them. Test groups also have
local `CMakeLists.txt` files and link only the component they exercise.

## Build dependencies

An arrow means **uses / links to**, not event flow:

```mermaid
flowchart TD
    App["app: tickforge"] --> Core["core: tickforge_core"]
    App --> Console["console: tickforge_console"]
    App -. "capture enabled" .-> Live["adapters/binance: tickforge_binance"]
    Console --> Core
    Live --> Core
    Live --> Console
    Live --> External["Boost.Beast / OpenSSL / nlohmann-json / Threads"]
```

- `core` uses the C++ standard library only. It has no terminal or network dependency.
- `console` publicly depends on `core` because its public functions/classes use core
  types such as `OrderBook` and `BookTracker`.
- `adapters/binance` publicly depends on `core` because its normalization API returns core
  events. It privately depends on `console` for capture diagnostics and on external
  libraries for its implementation. It does not expose JSON/Boost/OpenSSL types.
- `app` privately links the components it calls. Its capture feature switch is
  contained in `capture_command.cpp`.

In CMake, `PUBLIC` makes usage requirements available to consumers as well as the
target itself. `PRIVATE` keeps compile-time usage requirements within the target.
Static libraries can still require their private dependencies at final link time;
CMake carries that link requirement automatically.

## Core files

| Header / implementation | Responsibility |
| --- | --- |
| `events.hpp` | Shared vocabulary: times, trades, levels, snapshots, depth ranges, and local resets. `MarketEvent` holds one of these payloads. |
| `order_book.hpp` / `.cpp` | Known price levels in ordered maps, absolute-size updates, best bid/ask, spread, and range validation. |
| `book_tracker.hpp` / `.cpp` | Decides when the book may be exposed. Buffers depth around REST snapshots, establishes a stream bridge, and hides quotes after resets/gaps. |
| `recorder.hpp` / `.cpp` | Creates a new recording exclusively, writes normalized events in order, flushes them, and marks successful completion with a footer. |
| `replay.hpp` / `.cpp` | Reads and validates v1/v2/v3 sessions; invokes a callback once per event in recorded order. No sleeping or network access. |

`OrderBook` answers **what levels are known?** `BookTracker` answers **can consumers
use those levels now?** This distinction prevents a REST snapshot from looking
usable before its relationship to the live stream has been verified.

## Venue adapter files

Each venue owns a target, include path, and namespace. Binance uses
`tickforge_binance`, `<tickforge/adapters/binance/...>`, and
`tickforge::adapters::binance`. This keeps venue-specific parsers from colliding
when another exchange is added. `TICKFORGE_ENABLE_LIVE` remains the existing build
option for public network capture; it is not a target or an abstract source API.

Replay remains in `core` as session-file I/O. Network adapters and replay produce
the same `MarketEvent` types; a common virtual source interface is not implemented.

`binance_protocol.hpp/.cpp` translates Binance JSON into shared events and checks
decimal precision, tick/quantity steps, symbols, IDs, and timestamps. It contains
no sockets, making its inputs easy to test with synthetic JSON.

`capture.hpp/.cpp` provides the `adapters::binance::capture` entry point. Its private
`HttpsGet` handles REST transport, `DepthSocket` handles WebSocket transport, and
`Capture` coordinates metadata, recording, book tracking, timers, and recovery.
There is one application event loop.

## Console and application files

`console.hpp/.cpp` contains reusable exact decimal formatting, book/status text,
and checked console writes. `replay_reporter.hpp/.cpp` prints an event, updates its
`BookTracker`, counts event types/warnings, and prints the final replay summary.
Its per-event overloads make the handling of each payload explicit.

The application call chain is deliberately small:

```mermaid
flowchart LR
    Main["main.cpp"] --> CLI["cli.cpp: app::run"]
    CLI --> Replay["replay_command.cpp"]
    CLI --> Capture["capture_command.cpp"]
    Replay --> Reader["core: replay"]
    Reader --> Reporter["console: ReplayReporter"]
    Capture --> Adapter["adapters::binance::capture"]
```

`main.cpp` delegates. `cli.cpp` checks command shape and duration, shows usage,
dispatches, and maps failures to exit codes. `replay_command.cpp` opens the file
and connects `replay` to `ReplayReporter`. `capture_command.cpp` calls the optional
adapter, or reports that capture was not enabled in this build.

## Data flow

Capture and replay use the same event types and the same book-tracking code, but
they have separate instances of the state:

```mermaid
flowchart LR
    Binance["Binance REST + WebSocket"] --> Decode["Binance adapter: normalize JSON"]
    Decode --> Event["MarketEvent"]
    Event --> Write["SessionRecorder"]
    Write --> File["session.csv"]
    Event --> LiveBook["BookTracker: live instance"]
    File --> Read["replay"]
    Read --> ReplayBook["BookTracker: reporter instance"]
    LiveBook --> Quotes["usable bid / ask / spread"]
    ReplayBook --> Quotes
```

A reset makes quotes unavailable. During startup, depth can arrive before the REST
snapshot: it is recorded and buffered. Once a snapshot and a bridging range agree,
quotes become usable. On replay the same recorded order reconstructs those same
availability transitions without needing future data.

## Market data versus order execution

A market-data adapter receives prices, trades, and book changes. The current
Binance adapter only reads public depth and metadata. It has no account access.

An order-execution adapter would send orders/cancellations and consume order-status
and fill reports. It is a separate responsibility, not implemented here. A strategy
should eventually depend on an execution interface; the first implementation will
be a local simulator. A real venue execution adapter would require an explicit
later stage, authentication, reconciliation, and handling uncertain request results.
Do not add real execution to the public market-data adapter implicitly.

## Working in stages

`AGENTS.md` records the project's layout and collaboration rules. Each completed
stage should finish with relevant checks, documentation updates, and a local Git
commit. Network access is not required for CTest. Publishing commits requires a
separate user request.

The executable remains at `build/tickforge` (or the existing configuration-specific
Windows output location), so the documented run commands do not change. After a
layout change, reconfigure CMake; it will discover the new target locations.
For IDE setup, preset selection, and GDB startup items, see
[Visual Studio + WSL](visual-studio-wsl.md).
