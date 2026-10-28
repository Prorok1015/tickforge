#include <tickforge/strategy/threshold.hpp>
#include <tickforge/strategy/threshold_maker.hpp>
#include <stdexcept>
#include <cstdio>

using namespace tickforge;
void check(bool ok) { if (!ok) throw std::runtime_error("strategy contract failed"); }
// The first order a strategy places on this view, if any.
std::optional<Order> decide(strategy::Strategy& algorithm, const strategy::View& view)
{
    for (const auto& action : algorithm.on_market(view))
        if (const auto* place = std::get_if<strategy::Place>(&action)) return place->order;
    return std::nullopt;
}

void maker()
{
    using strategy::View;
    // Buy at <= 100.00, sell at >= 102.00, lot 0.002 BTC, taker 0.1%, maker 0.05%.
    strategy::ThresholdConfig config{10000, 10200, 200000, 1000000000, 1000, 1000, 0, 0, 500};
    strategy::ThresholdMaker algorithm(config);
    View view{{1, 0, 0, 0}, true, Level{9900, 1000000}, Level{10000, 1000000}, {10000000000, 0, 0, 0}, {}};
    auto actions = algorithm.on_market(view);
    const auto* bid = actions.size() == 1 ? std::get_if<strategy::Place>(&actions[0]) : nullptr;
    // The bid joins the best bid (99.00) because the threshold (100.00) would cross the ask.
    check(bid && bid->signal == strategy::Signal::buy && bid->order.type == OrderType::limit &&
          bid->order.post_only && bid->order.limit_price_atoms == 9900 && bid->order.quantity_atoms == 200000 &&
          bid->order.max_spend_quote_atoms == 0);
    const auto bid_order = bid->order;
    view.time.ordinal = 2;
    view.open_orders = {{bid_order, 0, false}};
    check(algorithm.on_market(view).empty()); // A resting bid is left alone, not chased.
    // Half the bid filled: stop the entry, and wait for the cancel before selling.
    view.time.ordinal = 3;
    view.balances = {9990095050, 9900000, 100000, 0};
    view.position = {9904950, 0};
    view.open_orders = {{bid_order, 100000, false}};
    actions = algorithm.on_market(view);
    check(actions.size() == 1 && std::get_if<strategy::Cancel>(&actions[0]) &&
          std::get<strategy::Cancel>(actions[0]).order_id == bid_order.id);
    view.time.ordinal = 4;
    view.open_orders[0].cancel_requested = true;
    check(algorithm.on_market(view).empty()); // The cancel is not repeated.
    // The bid is gone: ask for the 0.001 BTC held, never below 102.00 or break-even.
    view.time.ordinal = 5;
    view.balances = {9990095050, 0, 100000, 0};
    view.open_orders.clear();
    check(strategy::break_even_price(100000, view.balances, view.position, 500) == 9910);
    auto ask = decide(algorithm, view);
    check(ask && ask->side == Side::sell && ask->type == OrderType::limit && ask->post_only &&
          ask->limit_price_atoms == 10200 && ask->quantity_atoms == 100000);
    // Above the threshold the ask moves up to one tick over the bid, so it never crosses.
    strategy::ThresholdMaker high(config);
    view.bid = Level{10300, 1000000}; view.ask = Level{10400, 1000000};
    ask = decide(high, view);
    check(ask && ask->limit_price_atoms == 10301);
    // In reduce-only a flat maker places no bid.
    strategy::ThresholdMaker restricted(config);
    View flat{{1, 0, 0, 0}, true, Level{9900, 1000000}, Level{10000, 1000000}, {10000000000, 0, 0, 0}, {}, false, true};
    check(restricted.on_market(flat).empty());

    // Stop-loss with a resting ask: cancel the ask first, then sell at market.
    config.stop_loss_ppm = 10000;
    strategy::ThresholdMaker stopping(config);
    View held{{1, 0, 0, 0}, true, Level{9700, 1000000}, Level{9800, 1000000}, {9990095050, 0, 100000, 100000}, {9904950, 0}};
    held.open_orders = {{Order{7, Side::sell, 100000, 0, OrderType::limit, 10200, true}, 0, false}};
    actions = stopping.on_market(held);
    check(actions.size() == 1 && std::get<strategy::Cancel>(actions[0]).order_id == 7);
    held.time.ordinal = 2;
    held.balances.base_reserved = 0;
    held.open_orders.clear();
    actions = stopping.on_market(held);
    const auto* stop = actions.size() == 1 ? std::get_if<strategy::Place>(&actions[0]) : nullptr;
    check(stop && stop->signal == strategy::Signal::stop_loss && stop->order.type == OrderType::market &&
          stop->order.side == Side::sell && stop->order.quantity_atoms == 100000);
}
int main()
{
    try {
        strategy::Threshold algorithm({10000, 10200, 200000, 1000000000, 1000, 0});
        strategy::Strategy& interface = algorithm;
        strategy::View view{{1, 0, 0, 0}, false, Level{9900, 1000000}, Level{10000, 100000},
            {10000000000, 0, 0, 0}, {}};
        check(!decide(interface, view)); // Recovery/stale data must not trigger.
        ++view.time.ordinal; view.available = true;
        auto order = decide(interface, view);
        check(order && order->id == 1 && order->side == Side::buy && order->quantity_atoms == 200000);
        // Rejection leaves the account flat: no fictitious inventory transition.
        ++view.time.ordinal;
        order = decide(interface, view);
        check(order && order->side == Side::buy);
        // A partial fill is the only inventory we may sell, rounded down to step.
        ++view.time.ordinal; view.balances.base_total = 100500;
        check(!decide(interface, view));
        ++view.time.ordinal; view.bid = Level{10200, 1000000}; view.ask = Level{10300, 1000000};
        order = decide(interface, view);
        check(order && order->side == Side::sell && order->quantity_atoms == 100000 && order->id == 3);
        ++view.time.ordinal; view.balances.base_reserved = 1000;
        check(!decide(interface, view));
        ++view.time.ordinal; view.balances = {0, 0, 0, 0}; view.bid = Level{9900, 1000}; view.ask = Level{10000, 1000};
        check(!decide(interface, view)); // No spendable funds.
        ++view.time.ordinal; view.balances.quote_total = 10000000000; view.ask.reset();
        check(!decide(interface, view));
        ++view.time.ordinal; view.ask = Level{9800, 1000};
        check(!decide(interface, view)); // Crossed quotes are unusable.
        bool threw = false;
        try { decide(interface, view); } catch (const std::runtime_error&) { threw = true; }
        check(threw);
        threw = false;
        try { strategy::Threshold bad({10200, 10000, 1, 1, 1000, 0}); } catch (const std::runtime_error&) { threw = true; }
        check(threw);
        threw = false;
        try { strategy::Threshold bad({10000, 10200, 1000, 1, 1000, 1000001}); } catch (const std::runtime_error&) { threw = true; }
        check(threw);

        // Break-even selling: 0.002 BTC held at an average cost of 102.00 USDT, fee 0.1%.
        // The sale must return the cost after the fee: bid >= 102.00 / 0.999 = 102.102...
        strategy::Threshold careful({10000, 10200, 200000, 1000000000, 1000, 1000});
        strategy::View held{{1, 0, 0, 0}, true, Level{10200, 1000000}, Level{10300, 1000000},
            {0, 0, 200000, 0}, {20400000, 0}};
        check(!decide(careful, held)); // At the threshold, but the fee would realize a loss.
        ++held.time.ordinal; held.bid = Level{10210, 1000000};
        check(!decide(careful, held)); // 20420000 - 20420 fee < 20400000 cost.
        ++held.time.ordinal; held.bid = Level{10211, 1000000};
        order = decide(careful, held);
        check(order && order->side == Side::sell && order->quantity_atoms == 200000);
        // A thin best bid caps the sale (rounded to step) so it cannot slip to worse levels.
        ++held.time.ordinal; held.bid = Level{10211, 50500};
        order = decide(careful, held);
        check(order && order->side == Side::sell && order->quantity_atoms == 50000);
        ++held.time.ordinal; held.bid = Level{10211, 999};
        check(!decide(careful, held)); // Less than one quantity step is available.
        check(careful.last_signal() == strategy::Signal::none);

        // Stop-loss at 1% (10000 ppm) below the same 102.00 average cost:
        // limit = ceil(20400000 * 0.99) = 20196000 atoms, i.e. a bid of 100.98.
        strategy::ThresholdConfig stop_config{10000, 10200, 200000, 1000000000, 1000, 1000, 10000};
        strategy::Threshold stopping(stop_config);
        held.time.ordinal = 1; held.bid = Level{10099, 1000};
        check(!decide(stopping, held)); // 20198000 > limit: keep waiting.
        ++held.time.ordinal; held.bid = Level{10098, 1000};
        order = decide(stopping, held);
        check(order && order->side == Side::sell && order->quantity_atoms == 200000 &&
              stopping.last_signal() == strategy::Signal::stop_loss); // Whole position, not capped by the thin bid.
        ++held.time.ordinal; held.bid = Level{10300, 1000000}; held.ask = Level{10400, 1000000};
        order = decide(stopping, held);
        check(order && order->quantity_atoms == 200000 && stopping.last_signal() == strategy::Signal::take_profit);
        stop_config.stop_loss_ppm = 0;
        strategy::Threshold unprotected(stop_config);
        held.time.ordinal = 1; held.bid = Level{5000, 1000000}; held.ask = Level{5100, 1000000};
        check(!decide(unprotected, held)); // Without a stop the policy holds at any loss.
        threw = false;
        stop_config.stop_loss_ppm = 1000000;
        try { strategy::Threshold bad(stop_config); } catch (const std::runtime_error&) { threw = true; }
        check(threw);

        // Cooldown: 1 s of recorded time, started only once the view confirms the exit.
        strategy::ThresholdConfig cool_config{10000, 10200, 200000, 1000000000, 1000, 1000, 10000, 1000000000};
        strategy::Threshold cooling(cool_config);
        strategy::View view_c{{1, 0, 0, 0}, true, Level{10098, 1000000}, Level{10099, 1000000},
            {0, 0, 200000, 0}, {20400000, 0}};
        check(decide(cooling, view_c) && cooling.last_signal() == strategy::Signal::stop_loss);
        check(!cooling.started_cooldown_until()); // A proposal alone proves nothing.
        view_c.time = {2, 0, 0, 100}; // Stop rejected: BTC is still held, so the stop retries.
        check(decide(cooling, view_c) && cooling.last_signal() == strategy::Signal::stop_loss &&
              !cooling.started_cooldown_until());
        view_c.time = {3, 0, 0, 200}; view_c.balances = {10000000000, 0, 0, 0}; view_c.position = {0, -300000};
        view_c.available = false; // Confirmed balances start the pause even without quotes.
        check(!decide(cooling, view_c) && cooling.started_cooldown_until() == 1000000200);
        view_c.time = {4, 0, 0, 1000000199}; view_c.available = true; // Buy condition holds below.
        view_c.ask = Level{9900, 1000000}; view_c.bid = Level{9800, 1000000};
        check(!decide(cooling, view_c) && !cooling.started_cooldown_until()); // Still cooling.
        view_c.time = {5, 0, 0, 1000000200};
        check(decide(cooling, view_c) && cooling.last_signal() == strategy::Signal::buy);
        // A take-profit proposal clears a pending stop, so it never starts a cooldown.
        strategy::Threshold profit(cool_config);
        view_c = {{1, 0, 0, 0}, true, Level{10098, 1000000}, Level{10099, 1000000}, {0, 0, 200000, 0}, {20400000, 0}};
        check(decide(profit, view_c) && profit.last_signal() == strategy::Signal::stop_loss);
        view_c.time = {2, 0, 0, 100}; view_c.bid = Level{10300, 1000000}; view_c.ask = Level{10400, 1000000};
        check(decide(profit, view_c) && profit.last_signal() == strategy::Signal::take_profit);
        view_c.time = {3, 0, 0, 200}; view_c.balances = {10000000000, 0, 0, 0}; view_c.position = {0, 100000};
        view_c.bid = Level{9800, 1000000}; view_c.ask = Level{9900, 1000000};
        check(decide(profit, view_c) && !profit.started_cooldown_until() && profit.last_signal() == strategy::Signal::buy);
        threw = false;
        try { strategy::Threshold bad({10000, 10200, 1000, 1, 1000, 1000, 0, 1}); } catch (const std::runtime_error&) { threw = true; }
        check(threw); // A cooldown without a stop-loss would silently do nothing.

        // Account risk state: reduce-only blocks buys but keeps exits; a halt blocks both.
        strategy::Threshold restricted(cool_config);
        strategy::View risky{{1, 0, 0, 0}, true, Level{9800, 1000000}, Level{9900, 1000000},
            {10000000000, 0, 0, 0}, {}, false, true};
        check(!decide(restricted, risky)); // Buy condition holds, but buys are not allowed.
        risky.time.ordinal = 2; risky.balances = {0, 0, 200000, 0}; risky.position = {20400000, 0};
        risky.bid = Level{10098, 1000000}; risky.ask = Level{10099, 1000000};
        check(decide(restricted, risky) && restricted.last_signal() == strategy::Signal::stop_loss);
        risky.time.ordinal = 3; risky.sells_allowed = false;
        check(!decide(restricted, risky)); // Halted: not even a stop-loss is proposed.
        maker();
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
