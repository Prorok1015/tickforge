#include <tickforge/execution/order_manager.hpp>
#include <tickforge/execution/simulator.hpp>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <utility>

namespace {
using namespace tickforge;
using namespace tickforge::execution;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "expected exception");
}
template<class Data> MarketEvent event(std::int64_t ordinal, Data data, std::int64_t elapsed = 0)
{
    return {{ordinal, 0, 0, elapsed}, std::move(data)};
}
PaperConfig config() { return {1000, 1, 1000000000, 1000000000, 100}; }

// Zero latency: an accepted order is reported by the very next collect().
OrderReport run(OrderManager& manager, const Order& order)
{
    if (auto rejected = manager.submit(order)) return *rejected;
    auto reports = manager.collect();
    check(reports.size() == 1 && reports.front().order.id == order.id && manager.in_flight() == 0,
          "zero-latency execution must report exactly this order at once");
    return reports.front();
}

void routing_and_liquidity()
{
    auto settings = config();
    Simulator simulator(settings);
    accounting::Account account(1000000000000, 0, 0); // 10000 USDT
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{100, {{9900, 200000000}, {10000, 100000000}},
        {{10200, 200000000}, {10100, 100000000}}}));
    auto result = run(manager, {1, Side::buy, 250000000, 30000000000});
    check(result.result.status == OrderStatus::filled && result.result.fills.size() == 2,
          "buy must walk asks across levels");
    check(result.result.fills[0].price_atoms == 10100 && result.result.fills[0].quantity_atoms == 100000000 &&
          result.result.fills[1].price_atoms == 10200 && result.result.fills[1].quantity_atoms == 150000000,
          "asks must be consumed cheapest first");
    check(account.balances().quote_total == 974574600000 && account.balances().base_total == 250000000 &&
          account.balances().quote_reserved == 0, "buy costs, fees, BTC and released budget must reconcile");
    result = run(manager, {2, Side::buy, 100000000, 20000000000});
    check(result.result.status == OrderStatus::partially_filled && result.result.remaining_quantity_atoms == 50000000,
          "second order must see previously consumed liquidity");
    check(account.balances().quote_total == 969469500000, "partial fill cost must be exact");
    auto before = account.balances();
    result = run(manager, {3, Side::buy, 10000000, 2000000000});
    check(result.result.status == OrderStatus::cancelled && result.result.fills.empty() && account.balances() == before,
          "exhausted asks must not refill per request");
    simulator.on_market_event(event(2, Trade{1, 100000000}));
    check(run(manager, {4, Side::buy, 10000000, 2000000000}).result.fills.empty(), "trades must not replenish book depth");
    simulator.on_market_event(event(3, BookUpdate{101, 101, {{10000, 100000000}}, {}}));
    check(run(manager, {5, Side::buy, 10000000, 2000000000}).result.fills.empty(), "bid updates must not refill asks");
    simulator.on_market_event(event(4, BookUpdate{102, 102, {}, {{10200, 100000000}}}));
    result = run(manager, {6, Side::buy, 100000000, 20000000000});
    check(result.result.status == OrderStatus::filled && result.result.fills[0].price_atoms == 10200,
          "an explicit ask update replenishes only that level");
    result = run(manager, {7, Side::sell, 150000000, 0});
    check(result.result.status == OrderStatus::filled && result.result.fills.size() == 2 &&
          result.result.fills[0].price_atoms == 10000 && result.result.fills[1].price_atoms == 9900,
          "sell must walk bids highest first");
    check(account.balances().base_total == 250000000 && account.balances().quote_total == 974194350000 &&
          account.balances().base_reserved == 0, "sell cash and commission must reconcile");
}

void budget_and_rounding()
{
    auto settings = config(); settings.quantity_step_atoms = 1000;
    Simulator simulator(settings);
    accounting::Account account(100000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{10000, 100000000}}, {{10100, 100000000}}}));
    const auto result = run(manager, {1, Side::buy, 2000, 101101});
    check(result.result.status == OrderStatus::partially_filled && result.result.fills[0].quantity_atoms == 1000 &&
          result.result.fills[0].quote_atoms == 101000 && result.result.fills[0].fee_quote_atoms == 101,
          "buy budget must include fees and respect quantity step");
    check(account.balances().quote_total == 99898899 && account.balances().quote_reserved == 0, "budget accounting");
    const auto before = account.balances();
    check(run(manager, {2, Side::buy, 1000, 1}).result.status == OrderStatus::cancelled &&
          account.balances() == before, "unaffordable lot must not create a fill or retain a reservation");

    settings.quantity_step_atoms = 1;
    Simulator tiny(settings);
    tiny.on_market_event(event(1, BookSnapshot{1, {{1, 100}}, {{2, 100}}}));
    auto rounded = tiny.execute({1, Side::buy, 1, 2});
    check(rounded.fills.size() == 1 && rounded.fills[0].quote_atoms == 1 && rounded.fills[0].fee_quote_atoms == 1,
          "buy notional and fee round up at the smallest money unit");
    rounded = tiny.execute({2, Side::sell, 1, 0});
    check(rounded.fills.size() == 1 && rounded.fills[0].quote_atoms == 0 && rounded.fills[0].fee_quote_atoms == 0,
          "sell proceeds round down");
}

void risk_and_freshness()
{
    auto settings = config(); settings.quantity_step_atoms = 1000;
    settings.max_order_quantity_atoms = 100000; settings.max_position_quantity_atoms = 150000;
    Simulator simulator(settings);
    accounting::Account account(10000000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    check(!run(manager, {1, Side::buy, 100000, 100000000}).accepted, "no snapshot must block orders");
    simulator.on_market_event(event(1, BookSnapshot{100, {{10000, 100000000}}, {{10100, 100000000}}}));
    const auto original = account.balances();
    check(!run(manager, {2, Side::sell, 100000, 0}).accepted, "no short selling");
    check(!run(manager, {3, Side::buy, 101000, 100000000}).accepted, "order size risk");
    check(!run(manager, {4, Side::buy, 999, 100000000}).accepted, "quantity step risk");
    check(!run(manager, {5, Side::buy, 100000, 10000000001}).accepted, "insufficient USDT");
    check(account.balances() == original, "risk rejection must not reserve or spend");
    check(run(manager, {6, Side::buy, 100000, 100000000}).accepted, "valid order accepted");
    const auto before = account.balances();
    check(!run(manager, {7, Side::buy, 100000, 100000000}).accepted, "position limit uses requested maximum quantity");
    check(!run(manager, {6, Side::buy, 100000, 100000000}).accepted && account.balances() == before,
          "duplicate order ID must not execute twice");
    simulator.on_market_event(event(2, Trade{10100, 1}, 101));
    check(!simulator.available() && !run(manager, {8, Side::sell, 100000, 0}).accepted, "stale depth must block decisions");
    simulator.on_market_event(event(3, BookUpdate{99, 100, {}, {}}, 102));
    check(!simulator.available(), "stale duplicate range must not refresh time");
    simulator.on_market_event(event(4, BookUpdate{101, 101, {}, {}}, 103));
    check(simulator.available(), "valid next range refreshes depth time");
    simulator.on_market_event(event(5, BookUpdate{103, 103, {}, {}}, 104));
    check(!simulator.available() && !run(manager, {9, Side::sell, 100000, 0}).accepted, "gap blocks orders");
    simulator.on_market_event(event(6, BookSnapshot{200, {{10000, 100000}}, {{10100, 100000}}, true}, 105));
    check(!simulator.available(), "live snapshot must still wait for a bridge");
    simulator.on_market_event(event(7, BookUpdate{201, 201, {}, {}}, 106));
    check(simulator.available(), "bridging range restores execution");
    simulator.on_market_event(event(8, BookReset{"disconnect"}, 107));
    check(!simulator.available(), "disconnect clears execution availability");
    check(account.balances() == before, "blocked orders do not alter account");
}

void partial_sell()
{
    const auto settings = config();
    Simulator simulator(settings);
    accounting::Account account(0, 200000000, 20000000000); // 2 BTC bought at 100.00
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{10000, 100000000}}, {{10100, 100000000}}}));
    check(simulator.executable_level(Side::sell)->quantity_atoms == 100000000 &&
          simulator.executable_level(Side::buy)->price_atoms == 10100, "untouched book is fully executable");
    const auto result = run(manager, {1, Side::sell, 150000000, 0});
    check(!simulator.executable_level(Side::sell) && simulator.executable_level(Side::buy)->quantity_atoms == 100000000,
          "consumed bids are no longer executable; asks are unaffected");
    check(result.result.status == OrderStatus::partially_filled && result.result.remaining_quantity_atoms == 50000000,
          "sell remainder must be cancelled when known bids are exhausted");
    check(account.balances().base_total == 100000000 && account.balances().base_reserved == 0 &&
          account.balances().available_base() == 100000000 && account.balances().quote_total == 9990000000,
          "partial sell must free unused BTC and charge fees only on actual fills");
    check(account.position() == accounting::Position{10000000000, -10000000},
          "selling at cost loses exactly the fee; half the cost remains");
    simulator.on_market_event(event(2, BookSnapshot{2, {{10000, 100000000}}, {{10100, 100000000}}}));
    check(run(manager, {2, Side::sell, 100000000, 0}).result.status == OrderStatus::filled &&
          account.balances().base_total == 0, "new snapshot replenishes known liquidity");
    check(account.position() == accounting::Position{0, -20000000}, "closing the position removes all cost");
    simulator.on_market_event(event(3, BookSnapshot{3, {}, {}}));
    const auto before = account.balances();
    check(run(manager, {3, Side::buy, 1, 1}).result.status == OrderStatus::cancelled && account.balances() == before,
          "empty book must not invent an execution price");
}

void latency()
{
    auto settings = config();
    settings.stale_after_ns = 1000;
    settings.order_latency_ns = 100;
    settings.report_latency_ns = 50;
    Simulator simulator(settings);
    accounting::Account account(1000000000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{9900, 100000000}}, {{10100, 100000000}}}, 0));
    check(!manager.submit({1, Side::buy, 100000000, 20000000000}) && manager.collect().empty(),
          "an accepted order is in flight, not executed");
    const auto reserved = account.balances();
    // The ask moves before the order arrives (t=50); an event exactly at the due time (t=100)
    // is still part of the book the order meets.
    simulator.on_market_event(event(2, BookUpdate{2, 2, {}, {{10100, 0}, {10200, 100000000}}}, 50));
    simulator.on_market_event(event(3, BookUpdate{3, 3, {}, {{10200, 0}, {10300, 100000000}}}, 100));
    check(manager.collect().empty() && account.balances() == reserved, "nothing executes before the due time passes");
    // The first later event triggers execution against the book as of t=100, then is applied.
    simulator.on_market_event(event(4, BookUpdate{4, 4, {}, {{10300, 0}, {10400, 100000000}}}, 101));
    check(manager.collect().empty() && account.balances() == reserved, "the report is still on its way back");
    simulator.on_market_event(event(5, Trade{10400, 1}, 150));
    auto reports = manager.collect();
    check(reports.size() == 1 && reports[0].result.fills.size() == 1 && reports[0].result.fills[0].price_atoms == 10300 &&
          reports[0].executed_ns == 100 && reports[0].reported_ns == 150 && account.balances().base_total == 100000000,
          "price from the book at execution time; balances change only when the report arrives");

    // A disconnect while the order is in flight: no trusted book at execution, so it is cancelled.
    check(!manager.submit({2, Side::sell, 100000000, 0}), "sell accepted");
    simulator.on_market_event(event(6, BookReset{"disconnect"}, 200));
    simulator.on_market_event(event(7, Trade{10400, 1}, 300));
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].result.status == OrderStatus::cancelled && reports[0].result.fills.empty() &&
          reports[0].interruption == Interruption::no_trusted_book && reports[0].executed_ns == 250 &&
          account.balances().base_reserved == 0 && account.balances().base_total == 100000000,
          "a cancelled in-flight order releases its reservation and changes no totals");

    // Market data ends: an order not yet executed is cancelled; an executed one still reports.
    simulator.on_market_event(event(8, BookSnapshot{8, {{9900, 100000000}}, {{10100, 100000000}}}, 400));
    check(!manager.submit({3, Side::sell, 50000000, 0}), "first sell accepted");
    simulator.on_market_event(event(9, BookUpdate{9, 9, {{9900, 100000000}}, {}}, 501));
    check(!manager.submit({4, Side::sell, 50000000, 0}), "second sell accepted");
    simulator.finish();
    reports = manager.collect();
    check(reports.size() == 2 && reports[0].order.id == 3 && reports[0].result.status == OrderStatus::filled &&
          reports[0].reported_ns == 550 && reports[1].order.id == 4 &&
          reports[1].interruption == Interruption::session_ended && reports[1].result.fills.empty() &&
          manager.in_flight() == 0 && account.balances().base_total == 50000000 && account.balances().base_reserved == 0,
          "end of data delivers executed reports and cancels unexecuted orders");
    rejects([&] { simulator.on_market_event(event(10, Trade{1, 1}, 600)); });
    settings.order_latency_ns = -1;
    rejects([&] { Simulator invalid(settings); });
}

void risk_limits()
{
    // Loss from the session start: reduce-only, buys rejected, sells still allowed.
    RiskGuard loss({500, 0, 0}, 10000);
    loss.observe(0, 10000);
    loss.observe(10, std::nullopt); // no trusted valuation: nothing is judged
    loss.observe(20, 9501);
    check(loss.state() == TradingState::normal && loss.buys_allowed(), "a loss below the limit keeps trading");
    loss.observe(30, 9500);
    check(loss.state() == TradingState::reduce_only && loss.trigger() == RiskTrigger::max_loss &&
          loss.triggered_ns() == 30 && loss.trigger_equity() == 9500 && !loss.buys_allowed() && loss.sells_allowed(),
          "reaching the loss limit switches to reduce-only");
    check(loss.admit({1, Side::buy, 1, 1}).has_value() && !loss.admit({2, Side::sell, 1, 0}),
          "reduce-only rejects buys and admits sells");
    loss.observe(40, 20000);
    check(loss.state() == TradingState::reduce_only && loss.peak_equity() == 20000, "a recovery does not lift the limit");

    // Drawdown from the peak, not from the start.
    RiskGuard drawdown({0, 300, 0}, 10000);
    drawdown.observe(0, 10500);
    drawdown.observe(1, 10201);
    check(drawdown.state() == TradingState::normal, "a drawdown below the limit keeps trading");
    drawdown.observe(2, 10200);
    check(drawdown.state() == TradingState::reduce_only && drawdown.trigger() == RiskTrigger::max_drawdown &&
          drawdown.peak_equity() == 10500, "a drawdown from the peak triggers even above the start");

    // Request rate in any 60 s of session time: the request over the limit halts everything.
    RiskGuard rate({0, 0, 2}, 10000);
    rate.observe(0, 10000);
    check(!rate.admit({1, Side::buy, 1, 1}) && !rate.admit({2, Side::buy, 1, 1}), "requests within the rate pass");
    rate.observe(60000000000, 10000); // the first two requests are now outside the window
    check(!rate.admit({3, Side::buy, 1, 1}) && !rate.admit({4, Side::buy, 1, 1}), "the window slides with session time");
    check(rate.admit({5, Side::sell, 1, 0}).has_value() && rate.state() == TradingState::halted &&
          rate.trigger() == RiskTrigger::order_rate && !rate.sells_allowed(), "a burst halts all trading, sells included");
    rate.observe(600000000000, 10000);
    check(rate.admit({6, Side::sell, 1, 0}).has_value(), "a halt is final for the session");

    RiskGuard off({}, 10000);
    off.observe(0, 1);
    for (std::int64_t id = 1; id <= 1000; ++id) check(!off.admit({id, Side::buy, 1, 1}), "zero limits are disabled");
    rejects([] { RiskGuard invalid({-1, 0, 0}, 0); });
    rejects([&] { off.observe(-1, 1); });

    // OrderManager consults the guard before anything else and records the reason.
    auto settings = config();
    Simulator simulator(settings);
    accounting::Account account(1000000000000, 100000000, 10000000000);
    RiskGuard guard({1, 0, 0}, 1010000000000);
    OrderManager manager(account, simulator, settings, &guard);
    simulator.on_market_event(event(1, BookSnapshot{1, {{9900, 100000000}}, {{10100, 100000000}}}));
    guard.observe(0, 1009999999999);
    const auto refused = run(manager, {1, Side::buy, 100000000, 20000000000});
    check(!refused.accepted && refused.result.reason.starts_with("risk: reduce-only after max_loss") &&
          account.balances().quote_reserved == 0, "a blocked buy is rejected without reserving funds");
    check(run(manager, {2, Side::sell, 100000000, 0}).result.status == OrderStatus::filled, "closing is still possible");
}

Order limit(std::int64_t id, Side side, std::int64_t quantity, std::int64_t price, bool post_only = false)
{
    return {id, side, quantity, 0, OrderType::limit, price, post_only};
}

void limit_queue_and_fills()
{
    auto settings = config();
    settings.maker_fee_ppm = 500;
    Simulator simulator(settings);
    accounting::Account account(1000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{9900, 400}, {9800, 1000}}, {{10100, 500}, {10200, 1000}}}));
    check(!manager.submit(limit(1, Side::buy, 100, 9900)), "a non-crossing limit buy is accepted");
    auto reports = manager.collect();
    check(reports.size() == 1 && !reports[0].final && reports[0].result.status == OrderStatus::open &&
          reports[0].result.fills.empty() && simulator.queue_ahead(1) == 400,
          "the order rests behind the 400 already visible at its price");
    // Reservation: 9900 notional + ceil(9.9) = 10 fee at the higher fee + 2 atoms per lot of slack.
    check(account.balances().quote_reserved == 9900 + 10 + 200 && reports[0].order.max_spend_quote_atoms == 10110,
          "a limit buy reserves its worst-case cost");
    simulator.on_market_event(event(2, Trade{9900, 100, 11, Aggressor::buy}));
    check(manager.collect().empty() && simulator.queue_ahead(1) == 400, "a buy aggressor never reaches a resting bid");
    simulator.on_market_event(event(3, Trade{9900, 350, 12, Aggressor::sell}));
    check(manager.collect().empty() && simulator.queue_ahead(1) == 50, "sells at our price consume the queue first");
    simulator.on_market_event(event(4, Trade{9900, 80, 13, Aggressor::sell}));
    reports = manager.collect();
    check(reports.size() == 1 && !reports[0].final && reports[0].result.fills.size() == 1 &&
          reports[0].result.fills[0].quantity_atoms == 30 && reports[0].result.fills[0].price_atoms == 9900 &&
          reports[0].result.fills[0].liquidity == Liquidity::maker && reports[0].result.fills[0].fee_quote_atoms == 2 &&
          account.balances().base_total == 30 && account.balances().quote_reserved > 0,
          "the part of a trade beyond the queue fills us at our price with the maker fee");
    simulator.on_market_event(event(5, BookUpdate{2, 2, {{9900, 900}}, {}}));
    check(simulator.queue_ahead(1) == 0, "new orders at our price join behind us");
    simulator.on_market_event(event(6, Trade{9800, 1, 14, Aggressor::sell}));
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].final && reports[0].result.status == OrderStatus::filled &&
          reports[0].result.fills[0].quantity_atoms == 70 && reports[0].filled_quantity_atoms == 100 &&
          reports[0].fee_quote_atoms == 2 + 4, "a trade below our bid means the market traded through us");
    check(account.balances().base_total == 100 && account.balances().quote_reserved == 0 &&
          account.balances().quote_total == 1000000 - 2970 - 2 - 6930 - 4, "maker fills and fees reconcile exactly");
}

void limit_crossing_post_only_and_cancel()
{
    auto settings = config();
    settings.maker_fee_ppm = 500;
    Simulator simulator(settings);
    accounting::Account account(1000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{9900, 400}}, {{10100, 500}, {10200, 1000}}}));
    const auto refused = run(manager, limit(1, Side::buy, 100, 10100, true));
    check(refused.accepted && refused.result.status == OrderStatus::rejected &&
          refused.result.reason == "post-only limit order would cross the spread" && account.balances().quote_reserved == 0,
          "post-only refuses to cross and releases its reservation");
    check(!manager.submit(limit(2, Side::buy, 600, 10100)), "a crossing limit buy is accepted");
    auto reports = manager.collect();
    check(reports.size() == 1 && reports[0].result.status == OrderStatus::open && reports[0].result.fills.size() == 1 &&
          reports[0].result.fills[0].liquidity == Liquidity::taker && reports[0].result.fills[0].quantity_atoms == 500 &&
          simulator.queue_ahead(2) == 0, "the crossing part takes at most the limit price; the rest rests first in line");
    // The asks move below our resting bid: those sellers would have hit us at our price.
    simulator.on_market_event(event(2, BookUpdate{2, 2, {}, {{10100, 0}, {10050, 30}}}));
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].result.fills.size() == 1 && reports[0].result.fills[0].price_atoms == 10100 &&
          reports[0].result.fills[0].quantity_atoms == 30 && reports[0].result.fills[0].liquidity == Liquidity::maker &&
          simulator.executable_level(Side::buy)->price_atoms == 10200,
          "a book crossing our price fills us from the crossing size and consumes it");
    check(manager.cancel(2) && !manager.cancel(2), "a cancel is requested once");
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].final && reports[0].result.status == OrderStatus::partially_filled &&
          reports[0].filled_quantity_atoms == 530 && reports[0].result.remaining_quantity_atoms == 70 &&
          account.balances().quote_reserved == 0 && account.balances().base_total == 530,
          "cancelling a partly filled order releases the rest of its reservation");
    check(!manager.cancel(99), "nothing to cancel for an unknown order");
    check(!run(manager, limit(3, Side::buy, 1, 0)).accepted, "a limit order needs a positive price");
    check(!run(manager, {4, Side::buy, 1, 100, OrderType::limit, 100}).accepted, "a limit order takes no budget");
    check(!run(manager, {5, Side::buy, 1, 100, OrderType::market, 0, true}).accepted, "post-only is for limit orders");
}

void limit_cancel_race_and_session_end()
{
    auto settings = config();
    settings.order_latency_ns = 100;
    settings.stale_after_ns = 1000000; // trades do not refresh depth; keep the snapshot trusted
    Simulator simulator(settings);
    accounting::Account account(1000000, 1000, 100000);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{9900, 400}}, {{10100, 500}}}, 0));
    check(!manager.submit(limit(1, Side::sell, 100, 10100)), "a limit sell is accepted");
    simulator.on_market_event(event(2, Trade{10000, 1, 21, Aggressor::sell}, 150));
    auto reports = manager.collect();
    check(reports.size() == 1 && reports[0].executed_ns == 100 && simulator.queue_ahead(1) == 500,
          "the order arrives after its latency and rests behind the visible asks");
    check(manager.cancel(1), "cancel requested at t=150, arriving at t=250");
    // Before the cancel arrives the market trades through our ask: the fill wins the race.
    simulator.on_market_event(event(3, Trade{10200, 5, 22, Aggressor::buy}, 200));
    simulator.on_market_event(event(4, Trade{10200, 5, 23, Aggressor::buy}, 300));
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].final && reports[0].result.status == OrderStatus::filled &&
          manager.in_flight() == 0 && account.balances().base_total == 900 && account.balances().base_reserved == 0,
          "a cancel arriving after the fill changes nothing");
    // A resting order at the end of the data is cancelled and releases its BTC.
    check(!manager.submit(limit(2, Side::sell, 100, 10500)), "another limit sell");
    simulator.on_market_event(event(5, Trade{10200, 5, 24, Aggressor::buy}, 450));
    check(manager.collect().size() == 1 && account.balances().base_reserved == 100, "the sell rests");
    check(manager.cancel_open(Side::buy) == 0, "cancel_open filters by side");
    simulator.finish();
    reports = manager.collect();
    check(reports.size() == 1 && reports[0].interruption == Interruption::session_ended &&
          reports[0].result.status == OrderStatus::cancelled && account.balances().base_reserved == 0,
          "resting orders end with the session");
}

// Test executions answer each submitted order with a scripted result.
class ScriptedExecution : public Execution {
public:
    bool available() const override { return true; }
    void submit(const Order& order) override { reports_.push_back({answer(order), 0, 0}); }
    void cancel(std::int64_t) override {}
    std::vector<ExecutionReport> take_reports() override { return std::exchange(reports_, {}); }
private:
    virtual ExecutionResult answer(const Order& order) = 0;
    std::vector<ExecutionReport> reports_;
};
class UncertainExecution final : public ScriptedExecution {
    ExecutionResult answer(const Order&) override { throw std::runtime_error("result unknown"); }
};
class MalformedExecution final : public ScriptedExecution {
    ExecutionResult answer(const Order& order) override
    { return {order.id, OrderStatus::filled, {}, 0, "invalid empty filled report"}; }
};
class DuplicateExecution final : public ScriptedExecution {
    ExecutionResult answer(const Order& order) override
    {
        const Fill fill{1, order.id, order.side, 100, 1, 1, 0};
        return {order.id, OrderStatus::filled, {fill, fill}, 0, "duplicate confirmed receipt"};
    }
};
class RejectedExecution final : public ScriptedExecution {
    ExecutionResult answer(const Order& order) override
    { return {order.id, OrderStatus::rejected, {}, order.quantity_atoms, "confirmed rejection"}; }
};
class StrangerExecution final : public ScriptedExecution {
    ExecutionResult answer(const Order& order) override
    { return {order.id + 100, OrderStatus::rejected, {}, order.quantity_atoms, "report for an unknown order"}; }
};

void uncertain_results()
{
    const auto settings = config();
    accounting::Account account(100, 0, 0);
    UncertainExecution execution;
    OrderManager manager(account, execution, settings);
    rejects([&] { run(manager, {1, Side::buy, 1, 10}); });
    check(account.balances().quote_reserved == 10 && account.balances().quote_total == 100,
          "unknown result must retain its reservation");
    MalformedExecution malformed;
    OrderManager other(account, malformed, settings);
    rejects([&] { run(other, {2, Side::buy, 1, 10}); });
    check(account.balances().quote_reserved == 20 && account.balances().base_total == 0,
          "malformed result must not silently release reserves or invent BTC");
    accounting::Account other_account(100, 0, 0);
    DuplicateExecution duplicate;
    OrderManager duplicate_manager(other_account, duplicate, settings);
    const auto report = run(duplicate_manager, {1, Side::buy, 1, 10});
    check(report.result.fills.size() == 1 && other_account.balances().base_total == 1 &&
          other_account.balances().quote_total == 99, "duplicate receipts must not duplicate journal fills or money");
    RejectedExecution rejected;
    OrderManager rejected_manager(other_account, rejected, settings);
    const auto before = other_account.balances();
    check(run(rejected_manager, {2, Side::buy, 1, 10}).result.status == OrderStatus::rejected &&
          other_account.balances() == before, "confirmed rejection releases the whole reservation");
    StrangerExecution stranger;
    OrderManager stranger_manager(other_account, stranger, settings);
    rejects([&] { run(stranger_manager, {3, Side::buy, 1, 10}); });
    check(other_account.balances().quote_reserved == 10 && stranger_manager.in_flight() == 1,
          "a report for an unknown order must not release the real order's reservation");
}
}

int main()
{
    try {
        routing_and_liquidity(); budget_and_rounding(); risk_and_freshness(); partial_sell(); uncertain_results();
        latency(); risk_limits();
        limit_queue_and_fills(); limit_crossing_post_only_and_cancel(); limit_cancel_race_and_session_end();
        std::fputs("Execution, risk, and liquidity checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Execution check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
