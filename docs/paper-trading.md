# Offline paper trading

This stage adds market-order simulation and a separate reusable account ledger.
It consumes existing replay sessions; it has no real order API, keys, or account
access. The separate [strategy command](strategies.md) generates automatic requests
through the same execution/accounting path. The ledger also keeps an average cost
basis and realized PnL; the report adds unrealized PnL at the last trusted bid.

## Run the complete example

From the repository root after building:

```sh
./build/tickforge paper examples/synthetic-book.csv --config examples/paper-orders.toml
```

Use the path of your actual build, such as `out/build/wsl-live-local/tickforge`.
The `paper` command works in both default and live-enabled builds, without network
dependencies or a network connection. For Visual Studio, set these arguments and
keep the repository root as the working directory:

```json
"args": ["paper", "examples/synthetic-book.csv", "--config", "examples/paper-orders.toml"]
```

The entirely synthetic example starts with 1000 USDT and no BTC. It submits eight
requests, demonstrates two partial buys, a sale across two bid levels, a rejected
request during a book gap, and trading after snapshot recovery. Expected results:

```text
Paper complete | 8 requests | 1 rejected | 8 fills | fees=0.78001350 USDT
  USDT    | total=609.20148650 | reserved=0.00000000 | available=609.20148650
  BTC     | total=0.00600000 | reserved=0.00000000 | available=0.00600000
  PNL     | cost basis=390.40093949 USDT | average cost=65066.83 USDT | realized=-0.39757401 USDT
  MARK    | bid 65000.00 USDT at event #8 (before exit fees and depth)
  VALUE   | BTC=390.00000000 USDT | equity=999.20148650 USDT
  TOTAL   | realized=-0.39757401 USDT | unrealized=-0.40093949 USDT | total=-0.79851350 USDT
```

The total is the change in equity: 999.20148650 - 1000 USDT. It is a result under
the stated execution assumptions, not evidence of a profitable method. Redirect
stdout to a local log if you want to save the human-readable operation journal.

## Machine-readable reports

Both `paper` and `strategy` accept an optional `--report PREFIX`:

```sh
mkdir -p reports
./build/tickforge paper examples/synthetic-book.csv --config examples/paper-orders.toml --report reports/example
```

This writes four CSV files next to the console journal. All values are integers in
the units below (price 1e-2 USDT, BTC and USDT amounts 1e-8); no decimals are formatted.

| File | Rows |
| --- | --- |
| `PREFIX-orders.csv` | One per order when it ends (rejections included), in report order: submitting event and time, order, signal (`script`, `buy`, `take_profit`, `stop_loss`), side, type, limit price, post-only, request, status, filled/remaining quantity, fills, fees, balances, cost basis, and realized PnL after the report, execution and report times (empty for requests rejected before execution), interruption (`none`, `no_trusted_book`, `session_ended`), then the reason. |
| `PREFIX-fills.csv` | One per confirmed fill, including fills of still-resting orders: submitting event and time, order, fill ID, side, price, quantity, notional, fee, execution time, `taker` or `maker`, then the [fill diagnostics](#fill-diagnostics-and-markouts): source, queue at entry and level share at fill time (resting fills only), time since the last data gap (empty if none). |
| `PREFIX-markouts.csv` | One per fill once its last horizon is measured (or the session ended): fill, order, side, liquidity, price, quantity, execution time, and the markout after 1 s, 10 s, and 60 s in signed quote atoms (empty when the session ended first). |
| `PREFIX-summary.csv` | `key,value` rows: format and version (currently 6), inputs (session, config path, overrides, account, every execution parameter including latencies, strategy parameters, units), counts including cancelled in-flight orders and fills by source, fills near a data gap, the largest level share, markout totals per liquidity and horizon (fills, quantity, quote atoms), final balances and PnL, mark, and `status,complete` last. |

Rules that keep experiments comparable and honest:

- **Inputs are recorded with results.** A summary lists every parameter that
  produced it, so it can be reproduced from the same session file without guessing.
- **Nothing is overwritten.** All four files must be new; an existing report fails
  the run before any market event is processed.
- **Completion is explicit.** Order and fill journals grow during the run. The
  summary is created only after they are closed, and its last row is
  `status,complete`. A failed run leaves journals without a summary; treat them as
  incomplete, like a recording without its footer.
- **Unavailable is empty, not zero.** If BTC is held and no trusted bid was ever
  seen, the mark and valuation fields are empty.
- Text fields use standard CSV quoting only when needed (for example a path with a
  comma). The files have a header row and load directly into spreadsheet or
  dataframe tools.

Reports from private recordings are derived market data; keep them under the
ignored `reports/` or `recordings/` directory.

## Component boundaries

```mermaid
flowchart LR
    Script["App: scheduled order requests"] --> Manager["execution: OrderManager"]
    Manager --> Account["accounting: Account"]
    Manager --> Interface["execution: Execution interface"]
    Interface --> Sim["execution: Simulator"]
    Replay["core: replay events"] --> Sim
    Sim --> Tracker["core: BookTracker / known depth"]
    Sim -. "confirmed terminal result" .-> Manager
    Manager --> Journal["App: operation journal"]
```

- `tickforge_core` owns order/fill/result value types and checked integer arithmetic.
- `tickforge_accounting` owns the USDT/BTC ledger, order reservations, and applied
  receipt IDs. It depends only on core, not on the simulator, console, or a venue.
- `tickforge_execution` owns the execution interface, simulator, and order manager.
  It depends on accounting and core. The simulator itself does not change balances;
  the manager feeds confirmed fills into the ledger.
- The app reads a TOML config, connects replay to the simulator, submits scheduled
  requests, and prints journal entries and final balances.

The account is a starting point for reuse with future real execution reports. It
currently models one base/quote pair, nonnegative balances, and USDT fees. Durable
ledger persistence, multi-asset fees, rebates, deposits/withdrawals, exchange-state
reconciliation, and real execution are not implemented. The `Execution` contract is
asynchronous: `submit` hands over an order, and its confirmed terminal result is
delivered once, later, through `take_reports`. The manager commits only such results;
an unknown or inconsistent report keeps reservations and aborts the run.

## Config file (TOML, version 1)

An experiment is described in [TOML](https://toml.io/en/v1.0.0), read with the
vendored single-header [toml++](https://github.com/marzer/tomlplusplus) v3.4.0
(`third_party/tomlplusplus`, MIT). See `examples/paper-orders.toml` and
`examples/threshold.toml`.

```toml
version = 1

[account]
usdt = "1000"          # required
btc = "0"              # optional, default 0
btc_cost = "0"         # total USDT cost of the initial BTC; required when btc > 0

[execution]
fee = "0.1%"           # required; taker fee
maker_fee = "0.02%"    # optional; resting limit fills, default = fee
quantity_step = "0.00001"
max_order = "0.01"
max_position = "0.01"
stale_after = "5s"
order_latency = "0s"   # optional, see Latency
report_latency = "0s"  # optional

[risk]                 # optional; every limit defaults to off
max_loss = "5"         # USDT below the starting equity -> reduce-only
max_drawdown = "3"     # USDT below the session's peak equity -> reduce-only
max_orders_per_minute = 20  # more requests in any 60 s -> halt

[[orders]]             # repeated; or a [strategy] table, never both
after_event = 1        # send right after this market event ordinal
id = 1
side = "buy"           # "buy" or "sell"
quantity = "0.003"     # BTC
max_spend = "300"      # USDT budget including fees; market buys only

[[orders]]
after_event = 1
id = 2
side = "buy"
type = "limit"         # "market" (default) or "limit"
price = "64999.00"     # limit price; the budget is derived from it
post_only = true       # optional: reject instead of crossing the spread

[[cancels]]            # optional; sent after the orders of the same event
after_event = 3
id = 2
```

Values are written in human units and converted to integer atoms exactly:

| Kind | Written as | Stored as |
| --- | --- | --- |
| Price | `"81000.50"` (at most 2 decimals) | 1e-2 USDT |
| BTC quantity | `"0.00001"` (at most 8 decimals) | 1e-8 BTC |
| USDT amount | `"300"`, `"0.1"` (at most 8 decimals) | 1e-8 USDT |
| Percent | `"0.1%"` (at most 4 decimals, % required) | parts per million |
| Duration | `"100ns"`, `"250us"`, `"50ms"`, `"1.5s"`, `"2min"` (unit required) | nanoseconds |

Rules that keep a typo or an approximation from changing an experiment silently:

- **Decimals are strings.** A TOML float such as `quantity = 0.01` is a binary
  approximation and is rejected with a hint. Plain integers (`usdt = 1000`) are fine.
- **No rounding.** More decimals than the unit holds, signs, exponents, a missing
  unit or `%`, and overflow are errors.
- **Unknown keys are errors**, so `stop_los = "1%"` cannot leave the stop disabled.
- Errors name the key, for example `config orders[2].max_spend: is required`; TOML
  syntax errors give the line and column.

Initial BTC needs an explicit cost so that selling inherited inventory never shows a
fictitious profit. Orders run in file order; `after_event` must be positive and
nondecreasing, and IDs positive and unique. A sell has no `max_spend`. Invalid order
size, budget, and risk limits produce a rejected request in the journal.

### Command line and overrides

```sh
./build/tickforge paper SESSION --config FILE.toml [--set KEY=VALUE]... [--report PREFIX]
./build/tickforge strategy SESSION --config FILE.toml [--set KEY=VALUE]... [--report PREFIX]
```

`--set` replaces or adds one value by dotted key, for example
`--set execution.order_latency=50ms` or `--set strategy.stop_loss=0.3%`. Overrides
are applied in command-line order (a later one wins) before validation, so they obey
the same rules as file values. Only single values inside tables can be set; tables
and the `[[orders]]` list cannot. A missing `--config`, an unknown flag, or a flag
without a value prints usage and exits with code 2; an invalid config exits with 1.

The console header and the report summary record the config path, the overrides,
and every effective value after overriding, so a result can be reproduced from the
summary alone. The `paper` command requires `[[orders]]` (possibly none) and rejects
`[strategy]`; the `strategy` command requires `[strategy]`.

Orders are dispatched **after** applying the referenced market event and **before**
reading the next event. Multiple orders at the same ordinal see each other's
consumption. The script is a fixed experimental input, not a strategy. It may be
loaded in advance, but no future market event or price is preloaded for execution.
An order referring beyond the end of the session fails the experiment. An incomplete
v3 market recording also fails, even if earlier events already produced partial
console output; only `Paper complete` and a successful exit confirm completion.

## Units and rounding

| Value | Integer unit |
| --- | --- |
| Price | 0.01 USDT, as in the existing session formats |
| BTC quantity | 0.00000001 BTC |
| USDT balances, budgets, notionals, fees | 0.00000001 USDT |
| Fee rate | Parts per million; 1000 ppm = 0.1% |
| Simulation time and stale threshold | Monotonic session nanoseconds |

For example, `usdt = "1000"` initializes 100000000000 quote atoms. A buy budget of
`max_spend = "300"` reserves at most 30000000000 atoms (300 USDT) including commissions.

For each price-level fill, the quote amount is `price_atoms * quantity_atoms / 100`.
Buy notional rounds up; sell notional rounds down. The fee is
`ceil(quote_atoms * fee_ppm / 1000000)` and is charged in USDT per fill. There is no
floating-point calculation. Portable checked multiply/divide avoids overflowing
intermediate products; genuinely unrepresentable results fail rather than wrap.
Rounding may make very small simulated sales worth zero quote atoms; it never
creates a favorable rounding credit. The configured quantity step normally avoids
such dust in the example.

The fee and quantity step are explicit experimental assumptions. They are not
fetched automatically for a replay, and the sample is not a claim about a user's
current Binance fee tier. Venue minimum notional/order filters and fee discounts
are not fully emulated in this stage.

## Reservations, risk, and confirmed results

1. Validate the request and reject reused IDs, nonpositive/off-step quantity,
   invalid buy budget/sell budget, order-size excess, or unavailable/stale depth.
2. A buy's entire requested quantity must fit the BTC position limit, including
   pending buy quantities. This is conservative even if only part might fill.
3. Reserve the entire buy budget from available USDT, or the sell quantity from
   available BTC. Reservation reduces available funds without changing totals.
   A budget exceeding available cash is rejected even if expected fills cost less.
4. Submit to the execution interface. Buys walk asks cheapest first;
   sells walk bids highest first. No last-trade price is used as an execution quote.
   Fill in configured quantity-step multiples; buy sizing includes rounded fees.
5. Apply confirmed fills: purchases debit notional plus fee and credit BTC; sales
   debit BTC and credit proceeds minus fee. Only the executed part changes totals.
6. Cancel any unfilled remainder immediately and release its unused reservation.
   `partially_filled` is terminal here: the remaining quantity is cancelled.

Steps 1-4 happen at submission. Steps 5-6 happen when the manager collects the
execution report. With zero latency both occur within the event that submitted the
order; with latency they are separated in session time (see below).

No shorts, leverage, limit orders, resting orders, cancellation requests, or
calendar-day loss resets are implemented. A fresh sequence is not enough on its own: depth is
stale once the current replay offset exceeds the last valid depth observation by
`stale_after_ns`. Trades and stale duplicate ranges do not refresh this observation.
Gap/disconnect states and unbridged live snapshots block execution.

An identical already-applied fill ID is ignored, including after the order closes.
Conflicting data under the same fill ID fails. Duplicate receipts in a terminal
result are removed from the reported fills so the journal does not count them twice.
Balances and reservations are validated before committing each ledger mutation;
the manager validates a full result on a candidate ledger before committing it.

If the executor throws or returns an inconsistent result, the manager retains the
reservation and aborts the run. It must not assume that an uncertain request was
rejected. A confirmed rejection, in contrast, releases the reservation. Recovering
such an uncertain state through exchange reconciliation is a future real-execution
task; no real adapter is supplied by this stage.

## Risk limits

Strategy rules (stop-loss, cooldown, break-even selling) protect a strategy from its
own market view. The account-level `RiskGuard` protects the account from any
strategy, including a broken one. It sits in front of `OrderManager`, counts and
checks every request, and does not trust the strategy to obey.

| Limit | Measured as | Effect |
| --- | --- | --- |
| `max_loss` | starting equity minus current equity | **reduce-only** |
| `max_drawdown` | session peak equity minus current equity | **reduce-only** |
| `max_orders_per_minute` | requests in any 60 s of session time | **halted** |

- **Equity**, not realized PnL: USDT total plus BTC at the current trusted bid. A
  strategy that never sells below cost never realizes a loss, so a realized-only limit
  would stay blind exactly when the account loses value. Starting equity is the
  starting USDT plus the declared cost of starting BTC, as for PnL.
- Equity is judged only when the book is trusted at that event (or the account holds
  no BTC). During gaps the last state simply stays.
- **Reduce-only** rejects buys and still admits sells, so a losing position can be
  closed. Blocking everything would force the account to keep a falling position.
- **Halted** rejects everything. A burst of requests signals a malfunction rather than
  a market move, so even sells stop.
- States only escalate within a session (normal -> reduce-only -> halted); a limit
  that releases itself is not a limit. A new run starts fresh. Orders already in
  flight are not recalled: a submitted market order cannot be withdrawn.
- The strategy view carries `buys_allowed` and `sells_allowed`, so the threshold
  strategy stops proposing what would be refused. Refusals that still happen are
  journaled with a `risk:` reason.

The journal prints `Risk | event #N | REDUCE-ONLY ...` or `HALTED ...` with the
equity, start, and peak. The summary records the limits, the final `risk_state`, the
`risk_trigger`, its event and time, the equity then, and the peak equity.

A "day" is the session for now: recordings are at most an hour long. Calendar-day
resets matter only for multi-day runs and are not implemented. A manual kill switch
has no use in replay, where no one can press it mid-run; automatic triggers cover
that role here.

## Limit orders and the queue model

A limit order executes at its price or better. The part that crosses the spread on
arrival executes at once like a market order, but never beyond the limit, and pays the
taker fee. The rest **rests** in the book at its price and pays `maker_fee` when it is
filled. `post_only` rejects an order that would cross instead of letting it take.
Without `maker_fee`, resting fills pay the taker fee, so no discount is assumed silently.

Our resting order is invisible to the recorded market. Where it would stand in the real
queue and when it would fill must therefore be estimated from public data. Every rule
below is chosen so that the simulation errs towards **fewer** fills than reality:

| Event | Effect on our resting order |
| --- | --- |
| Arrival (after `order_latency`) | We join the **back** of the queue: everything visible at our price is ahead. An empty level puts us first. |
| Opposite-aggressor trade at our price | Consumes the queue ahead of us first; what remains reaches us. Several of our orders at one price share a trade in arrival order. Trades with an unknown aggressor (v1-v3 files) are ignored at our price. |
| Trade at a worse price (a *trade-through*) | Fills us completely: by price priority the venue would have matched us first. Valid for any aggressor. |
| The other side of the book reaches our price | Fills us from that crossing size, at our price, and consumes it virtually: those orders would have traded with us. |
| Our level shrinks without trades | Treated as cancellations **behind** us; the queue ahead only shrinks to the level's new size (it cannot exceed it). Growth is behind us. |

A fill of a resting order happens at our price (the maker's price), not at the
aggressor's. Each report carries only its new fills; the order stays open with its
reservation until a terminal report. A limit buy reserves its worst case: the whole
quantity at the limit price, the higher of the two fees, and two atoms of rounding
slack per lot; the unused part is released at the end.

**Cancels** travel with `order_latency`. A fill that happens before the cancel arrives
stands; a cancel that arrives after the order filled does nothing. A cancelled order
that had fills ends as `partially_filled`. Orders still resting when market data ends
end with interruption `session_ended`. When the risk guard enters reduce-only, resting
buys are cancelled; when it halts, all resting orders are.

What this model cannot know, and what to keep in mind when reading results:

- **Market impact.** In reality our order would have taken a place in the queue and
  absorbed trades that went to others. The larger our order relative to the level,
  the less the recorded market behind it is valid.
- **Adverse selection.** A resting order fills when the market moves through it, so
  fills cluster just before moves against the position. Saving the spread and the
  taker fee does not by itself make a profit.
- **Trade gaps.** Trades missed by the feed (see the replay `Trade feed` summary) may
  hide fills. Queue estimates after a reconnect rely on the new snapshot only.

## Fill diagnostics and markouts

The model above produces fills, but not all of them are equally believable. Every
simulated fill therefore carries the evidence behind it, printed after the run and
written to the reports:

```text
  FILLS   | take 1 | trade_through 0 | book_cross 0 | queue 1 | within 1 s of a data gap 0 | max level share 0.0500%
  MARKOUT | maker | 1s -0.00120000 USDT over 1 fills (-1.20 USDT/BTC) | 10s ... | 60s ...
  MARKOUT | taker | ...
```

**Source**, from the strongest evidence to the weakest for a resting order:

| Source | Evidence | Trust |
| --- | --- | --- |
| `take` | Our order crossed the spread and consumed visible depth. | As good as the depth view. |
| `trade_through` | The venue printed a trade beyond our price; by price priority we would have filled first. | Strongest for a resting order: a real trade, independent of queue position. |
| `book_cross` | The opposite side of the 100 ms depth stream reached our price. | Good, but no trade print confirms the size we took. |
| `queue` | Trades at our price used up our *estimated* queue position. | Weakest: depends on where we really stood and on whom the cancellations hit. |

**Queue at entry** is the visible size at our price when the order started resting;
we join behind all of it. **Level share** is measured when a fill happens: our resting
quantity before the fill divided by the size still visible at our price plus ours. It
is taken at fill time because an order placed away from the market often starts alone
at an empty price, which says nothing about impact; what matters is how large we are
when the market actually trades there. It is not a measure of strategy quality but of
how far the recording still describes a market with us in it: at a fraction of a
percent our order would hardly have been noticed; at tens of percent others would
have reacted, and the replay no longer shows what would have happened. A share near
100% means the level was empty apart from us at that moment, typically when the
price jumped through it.

**Near a data gap** counts fills that happened within 1 s after the last trade ID gap
or reorder, or after the book lost synchronization. Missed trades can make queue
estimates and trade-through fills wrong in either direction.

**Markout** measures what the market did right after each fill: the first trusted
mid price at or after `fill time + horizon` (1 s, 10 s, 60 s), compared with the fill
price, times the fill quantity. It is positive when the market moved in our favour
(above our buy price, below our sell price). Fees are not included. A resting order
fills exactly when someone is eager to trade against it, so maker markouts are
typically negative: this is adverse selection, the price paid for earning the spread.
A maker strategy is viable only if the spread it earns (plus any rebate) exceeds that
loss on average.

Markouts read market data from after the fill, so they are computed only in the
report layer and never reach a strategy: a decision that could see them would be
using the future (look-ahead bias). A report delayed past a horizon by
`report_latency` is measured when it arrives. Fills too close to the end of the
session leave the later horizons empty, which is "unknown", not zero.

## Latency

Two fixed delays, in recorded session nanoseconds (`receive_elapsed_ns`), separate a
decision from its consequences:

```text
t        decision; funds reserved                       ("Sent" in the journal)
t + L1   execution at the venue: decides the price      (order_latency_ns)
t + L1 + L2  report reaches us: balances, PnL, strategy  (report_latency_ns)
```

The recording already contains the delay of market data reaching us, because its
times are receive times. The model adds what a recording cannot show: our processing
and the order's trip to the venue (`L1`), and the confirmation's trip back (`L2`).
Both are experimental assumptions. Measure a real round trip with ping/pong or a
venue time request; `exchange_ns - receive_unix_ns` is not a latency measurement
because two different clocks produce it.

**Which book an order meets.** Events arrive only when the book changes, so the book
at `t + L1` is the book after the last event at or before `t + L1`. When replay
reaches the first event later than `t + L1`, the order executes first, against the
book not yet updated by that event, and then the event is applied. Waiting for a
later event would execute against a future book. With `L1 = 0` the order executes
at submission against the current book, as before.

**While an order is in flight** its reservation stays open, so the strategy makes no
decisions, and scripted orders still apply position limits to pending buys.
Staleness is judged at `t + L1`: depth older than `stale_after_ns` at that moment
counts as untrusted.

**Cancellations.** The venue would execute an order even if our feed broke, but
without a trusted book we cannot price it. Such an order is cancelled with
interruption `no_trusted_book`. An order still in flight when market data ends is
cancelled with `session_ended`. Both release the reservation and are counted in the
summary (`cancelled_no_trusted_book`, `cancelled_session_ended`). The first rule is
optimistic: in reality the order might have filled at a poor price, so a high count
means the result is less reliable. An order that executed before the data ended
still reports, even if its report time lies beyond the end.

Not modelled: random or load-dependent delays, faster participants taking the levels
we saw, and our order's effect on others. In a fast market a delayed market order in
the direction of the move can look better in simulation than it would be in reality.

## Cost basis and PnL

The ledger uses the **average cost** method for the single BTC position:

- A buy adds its notional **plus its fee** to the position cost.
- A sale removes the sold share of that cost: `cost * sold / held`, rounded up so
  rounding never adds to realized profit. Selling the whole position removes the
  exact remaining cost, so no rounding residue survives a closed position.
- Realized PnL changes only on sales: proceeds after the sale fee minus removed cost.
  It is therefore net of both commissions of a round trip.
- Initial BTC requires an explicit `account.btc_cost`.

The cost is stored as a total in 1e-8 USDT rather than as an average price, so
partial sales do not accumulate price-rounding error. The journal prints the
average cost (price units, rounded up) for reading only.

Unrealized PnL is reported, not stored. The app remembers the last best bid seen
while execution was available, because recordings end with a shutdown reset that
hides quotes. Held BTC is valued at that bid, rounded down like a sale:

```text
unrealized = held BTC * mark bid - remaining cost
total      = realized + unrealized = equity - initial equity
equity     = USDT total + held BTC * mark bid
```

Initial equity is initial USDT plus the declared cost of initial BTC. The mark is
the price a market sale would start at; it excludes the exit fee and the depth a
real sale might cross, so it is optimistic for large positions. Without BTC no mark
is needed. With BTC and no trusted bid in the whole session, unrealized PnL is
reported as unavailable instead of being guessed. Strategies do not see PnL yet.

Tests check that equity change always equals realized plus unrealized PnL, across
partial sales, several marks, and inherited inventory.

## Virtual liquidity policy

Virtual requests do not alter the recorded market book. The simulator keeps
separate consumed-volume counters for bids and asks:

- Each fill consumes known size; another request cannot reuse it immediately.
- Trades, duplicate depth ranges, and updates to unrelated prices/sides do not
  replenish consumed levels.
- An accepted explicit absolute-size update replenishes only that price level.
  A new snapshot starts a new observation and clears all consumption counters.
- Invalid/reset states hide depth and clear the old counters; recovery needs a
  snapshot/bridge before execution becomes available again.

Treating a new public level update as replenishment is a simplifying assumption,
not proof of newly arrived liquidity or exact own market impact. Only known levels
can fill; deeper unknown liquidity is not invented. Prices/volumes are exogenous,
and queue position is not modeled.

## Validation

Tests cover exact cash/BTC/fee accounting, reservations and release, duplicate and
conflicting fills, checked arithmetic boundaries, bid/ask routing, multiple price
levels, partial buys/sells, budget-plus-fee sizing, virtual depletion/replenishment,
position/order limits, stale/gapped/reset books, uncertain results, scenario errors,
incomplete market input, and deterministic full CLI output. Tests require no network
and use explicit checks in both Debug and Release builds.
