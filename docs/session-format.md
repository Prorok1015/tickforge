# Session formats v1 to v4

These small offline formats carry normalized market events for Binance Spot BTCUSDT.
They are not Binance wire payloads or a complete live capture format. All checked-in
examples are synthetic. Version 1 remains supported unchanged for trades; version 2
adds order book snapshots and depth updates. Version 3 adds capture lifecycle,
pending snapshots, and a mandatory completion footer. Version 4 adds the venue trade
ID and aggressor side to trade rows. Live capture writes version 4.

The first line for v1 is:

```text
tickforge,1,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000
```

The v2 header has the same fields, with `2` in place of `1`. A trade row in either
version contains seven comma-separated fields:

```text
trade,ordinal,exchange_ns,receive_unix_ns,receive_elapsed_ns,price_atoms,quantity_atoms
```

- All numbers are nonnegative signed 64-bit integers written as decimal digits.
  Trade price and quantity must be positive. No signs, whitespace, fractions, quoting,
  comments, blank lines, or additional fields are accepted.
- `ordinal` starts at 1 and increases by exactly one. It is a local recording order,
  not an exchange trade ID or depth sequence number.
- `exchange_ns` and `receive_unix_ns` are separate Unix timestamps in nanoseconds.
  Converting a millisecond timestamp to this unit does not improve its precision.
  Neither field is required to increase. Their difference is not a network-latency
  measurement.
- `receive_elapsed_ns` is a monotonic offset from the recording session's origin.
  It may stay equal but must never decrease. Replay advances immediately to this
  offset, without sleeping or consulting the wall clock. The first offset may be
  greater than zero.
- Price is in 1/100 USDT; quantity is in 1/100000000 BTC. For example, `6500000`
  means 65000.00 USDT and `100000` means 0.001 BTC. These are representation units,
  not claims about current Binance tick/lot filters. Live normalization must check
  current instrument metadata and reject unrepresentable precision; no silent
  rounding is allowed. No multiplication or accounting is implemented yet.

LF and CRLF are accepted, as is a final line without a newline. A valid header with
no events is an empty session. Unsupported versions/instruments/scales fail closed.

Replay validates and delivers one event at a time through a synchronous callback.
There is no sorting, preloading, randomness, or wall-clock dependency. The callback
receives the current event, not the future file contents; future strategy code must
preserve that boundary. Recorded order and elapsed offsets are the simulation
timeline, even when exchange timestamps are out of order.

On malformed input or an I/O error, replay stops with a nonzero CLI exit code.
Already emitted events remain emitted, so partial stdout from a failed run must not
be treated as a completed experiment. The CLI uses `std::format` to display labeled
event blocks, decimal prices/quantities, the local book state, and a final summary.
This human-readable output is not a session recording format. Decimal display uses
integer division and remainder, without conversion to floating point.

## Version 2 book events

```text
snapshot,ordinal,exchange_ns,receive_unix_ns,receive_elapsed_ns,last_update_id,bid_count,ask_count,bid_price,bid_qty,...,ask_price,ask_qty,...
depth,ordinal,exchange_ns,receive_unix_ns,receive_elapsed_ns,first_update_id,last_update_id,bid_count,ask_count,bid_price,bid_qty,...,ask_price,ask_qty,...
```

The counts describe the number of price/quantity pairs on each side. All bid pairs
precede all ask pairs; either count may be zero. Counts must match the row exactly.
Prices must be positive. Zero depth quantity deletes a level; other depth quantities
replace the entire size at that price. Within one update repeated prices are applied
in listed order (last value wins). Snapshot quantities must be positive and snapshot
prices must be unique within each side. Both sides are applied as one event before
validating the resulting book. An empty or one-sided book is representable; missing
quotes and spreads are displayed as unavailable, not zero.

Snapshot `exchange_ns=0` denotes an unavailable exchange timestamp, as in the
synthetic example; the live REST snapshot is not assumed to provide an exchange time.
Depth ranges must have `first_update_id <= last_update_id`. IDs are separate from
the local `ordinal` and use the same nonnegative int64 representation.

## Book integrity and recovery

A snapshot replaces the local book. Depth is ignored until a snapshot is supplied.
Once synchronized, ranges with a final ID no newer than the current book are ignored.
A range covering the next ID is applied; a range starting after it indicates a gap.
A gap clears the book and hides quotes until a fresh snapshot. Invalid snapshots,
invalid level values, or locked/crossed books (bid >= ask) also clear the book.
Trades never change depth quantities, avoiding double counting alongside depth events.

Book integrity warnings are displayed in the event and counted in the summary.
Replay continues so that a later recorded snapshot can demonstrate recovery. Exit
code 0 means the input was consumed, not that every interval had a trustworthy book.
Consumers must check book state before using quotes. There is no strategy yet.

These are local normalized-event rules based on the
[Binance Spot book update procedure](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md),
checked on 2026-10-24. The optional live adapter buffers WebSocket updates around
REST snapshots and validates the synchronization bridge through `BookTracker`;
v3 lifecycle events make this behavior reproducible offline. Version 2 recorded
snapshots are trusted as synchronization boundaries. Only known levels
are stored; bounded snapshots cannot establish the full depth of the real market.
Version 3 live snapshots use the stricter bridge procedure below.

## Version 3 live recordings

The header uses `3` instead of `1` or `2`; scales and instrument are unchanged.
Trade/depth rows retain their syntax. Additional rows are:

```text
reset,ordinal,0,receive_unix_ns,receive_elapsed_ns,reason
snapshot_pending,ordinal,0,receive_unix_ns,receive_elapsed_ns,last_update_id,bid_count,ask_count,...pairs
end,event_count
```

`reason` is a nonempty lowercase ASCII identifier with optional underscores, such as
`connecting`, `disconnect`, `stale_depth`, or `shutdown`. Reset clears all quotes and
any bootstrap buffer. Depth received after a reset is recorded in receipt order and
buffered internally. A pending snapshot is not exposed as usable until a subsequent
or already-buffered range bridges its update ID. All-stale ranges do not establish
the bridge. This same state machine runs during capture and replay.

`end` must be the final row, and its event count must equal the last ordinal (zero
for an empty completed session). Missing/mismatched footers or trailing rows are
errors. Rows may already have been delivered when an incomplete file is detected;
only a successful exit confirms complete input. Flushes protect against application
buffer loss but are not a disk durability guarantee (`fsync` is not implemented).

## Version 4 trades

The header uses `4`. Everything in version 3 is unchanged except the trade row, which
gains two required fields:

```text
trade,ordinal,exchange_ns,receive_unix_ns,receive_elapsed_ns,price_atoms,quantity_atoms,trade_id,aggressor
```

- `trade_id` is the venue's positive trade ID. Binance IDs are consecutive per symbol,
  so a jump means trades were missed, typically during a reconnect.
- `aggressor` is `buy` or `sell`: the side that crossed the spread. A buy aggressor
  lifted asks and a sell aggressor hit bids. Binance reports `m` (buyer is maker); `m`
  true means a sell aggressor. A resting simulated buy order can only be filled by a
  sell aggressor at its price, which is why the side matters for limit orders.
- For live trades `exchange_ns` is the trade time `T`, not the event time `E`.

Versions 1-3 keep seven-field trade rows; their trades have an unknown ID and side.
Trades never change book quantities and never refresh depth staleness, in any version.
Replay reports each trade's ID and side and counts ID gaps or reorders in its summary
(`Trade feed | N ID gaps or reorders`). A trade gap leaves the book valid but makes
traded volume at a price incomplete; it is reported, not treated as a book reset.

The live adapter checks a five-second depth timeout and records resets; offline
replay follows those recorded lifecycle events without consulting wall-clock time.
See [live capture](live-capture.md) for build commands, recovery, and known limits.

Run from the repository root after building:

```sh
./build/tickforge replay examples/synthetic-book.csv
```

The example demonstrates snapshot ID 100, absolute updates, a trade, a sequence gap,
ignored depth while waiting, recovery at snapshot ID 110, a stale range, and an
overlapping range ending at ID 112. Selected output:

```text
  BID    | 64999.00 USDT x 0.00200000 BTC
  ASK    | 65001.00 USDT x 0.00300000 BTC
  SPREAD | 2.00 USDT (ask - bid)
...
  STATUS | GAP | book cleared; fresh snapshot required
...
Replay complete | 8 events: 1 trades, 2 snapshots, 5 depth updates
Integrity       | 2 warnings | final book: synchronized (known levels only)
```

The original `examples/synthetic.csv` remains a supported trade-only v1 example.
