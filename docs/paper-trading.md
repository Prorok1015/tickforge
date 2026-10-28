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

This writes three CSV files next to the console journal. All values are integers in
the units below (price 1e-2 USDT, BTC and USDT amounts 1e-8); no decimals are formatted.

| File | Rows |
| --- | --- |
| `PREFIX-orders.csv` | One per request, including rejections, in report order: submitting event and time, order, signal (`script`, `buy`, `take_profit`, `stop_loss`), side, request, status, filled/remaining quantity, fills, fees, balances, cost basis, and realized PnL after the report, execution and report times (empty for requests rejected before execution), interruption (`none`, `no_trusted_book`, `session_ended`), then the reason. |
| `PREFIX-fills.csv` | One per confirmed fill: submitting event and time, order, fill ID, side, price, quantity, notional, fee, execution time. |
| `PREFIX-summary.csv` | `key,value` rows: format and version (currently 3), inputs (session, config path, overrides, account, every execution parameter including latencies, strategy parameters, units), counts including cancelled in-flight orders, final balances and PnL, mark, and `status,complete` last. |

Rules that keep experiments comparable and honest:

- **Inputs are recorded with results.** A summary lists every parameter that
  produced it, so it can be reproduced from the same session file without guessing.
- **Nothing is overwritten.** All three files must be new; an existing report fails
  the run before any market event is processed.
- **Completion is explicit.** Order and fill journals grow during the run. The
  summary is created only after both are closed, and its last row is
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
fee = "0.1%"           # required
quantity_step = "0.00001"
max_order = "0.01"
max_position = "0.01"
stale_after = "5s"
order_latency = "0s"   # optional, see Latency
report_latency = "0s"  # optional

[[orders]]             # repeated; or a [strategy] table, never both
after_event = 1        # send right after this market event ordinal
id = 1
side = "buy"           # "buy" or "sell"
quantity = "0.003"     # BTC
max_spend = "300"      # USDT budget including fees; buys only
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
daily-loss limits are implemented. A fresh sequence is not enough on its own: depth is
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
