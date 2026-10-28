#include <tickforge/strategy/threshold.hpp>
#include <stdexcept>
#include <cstdio>

using namespace tickforge;
void check(bool ok) { if (!ok) throw std::runtime_error("strategy contract failed"); }
int main()
{
    try {
        strategy::Threshold algorithm({10000, 10200, 200000, 1000000000, 1000, 0});
        strategy::Strategy& interface = algorithm;
        strategy::View view{{1, 0, 0, 0}, false, Level{9900, 1000000}, Level{10000, 100000},
            {10000000000, 0, 0, 0}, {}};
        check(!interface.on_market(view)); // Recovery/stale data must not trigger.
        ++view.time.ordinal; view.available = true;
        auto order = interface.on_market(view);
        check(order && order->id == 1 && order->side == Side::buy && order->quantity_atoms == 200000);
        // Rejection leaves the account flat: no fictitious inventory transition.
        ++view.time.ordinal;
        order = interface.on_market(view);
        check(order && order->side == Side::buy);
        // A partial fill is the only inventory we may sell, rounded down to step.
        ++view.time.ordinal; view.balances.base_total = 100500;
        check(!interface.on_market(view));
        ++view.time.ordinal; view.bid = Level{10200, 1000000}; view.ask = Level{10300, 1000000};
        order = interface.on_market(view);
        check(order && order->side == Side::sell && order->quantity_atoms == 100000 && order->id == 3);
        ++view.time.ordinal; view.balances.base_reserved = 1000;
        check(!interface.on_market(view));
        ++view.time.ordinal; view.balances = {0, 0, 0, 0}; view.bid = Level{9900, 1000}; view.ask = Level{10000, 1000};
        check(!interface.on_market(view)); // No spendable funds.
        ++view.time.ordinal; view.balances.quote_total = 10000000000; view.ask.reset();
        check(!interface.on_market(view));
        ++view.time.ordinal; view.ask = Level{9800, 1000};
        check(!interface.on_market(view)); // Crossed quotes are unusable.
        bool threw = false;
        try { interface.on_market(view); } catch (const std::runtime_error&) { threw = true; }
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
        check(!careful.on_market(held)); // At the threshold, but the fee would realize a loss.
        ++held.time.ordinal; held.bid = Level{10210, 1000000};
        check(!careful.on_market(held)); // 20420000 - 20420 fee < 20400000 cost.
        ++held.time.ordinal; held.bid = Level{10211, 1000000};
        order = careful.on_market(held);
        check(order && order->side == Side::sell && order->quantity_atoms == 200000);
        // A thin best bid caps the sale (rounded to step) so it cannot slip to worse levels.
        ++held.time.ordinal; held.bid = Level{10211, 50500};
        order = careful.on_market(held);
        check(order && order->side == Side::sell && order->quantity_atoms == 50000);
        ++held.time.ordinal; held.bid = Level{10211, 999};
        check(!careful.on_market(held)); // Less than one quantity step is available.
        check(careful.last_signal() == strategy::Signal::none);

        // Stop-loss at 1% (10000 ppm) below the same 102.00 average cost:
        // limit = ceil(20400000 * 0.99) = 20196000 atoms, i.e. a bid of 100.98.
        strategy::ThresholdConfig stop_config{10000, 10200, 200000, 1000000000, 1000, 1000, 10000};
        strategy::Threshold stopping(stop_config);
        held.time.ordinal = 1; held.bid = Level{10099, 1000};
        check(!stopping.on_market(held)); // 20198000 > limit: keep waiting.
        ++held.time.ordinal; held.bid = Level{10098, 1000};
        order = stopping.on_market(held);
        check(order && order->side == Side::sell && order->quantity_atoms == 200000 &&
              stopping.last_signal() == strategy::Signal::stop_loss); // Whole position, not capped by the thin bid.
        ++held.time.ordinal; held.bid = Level{10300, 1000000}; held.ask = Level{10400, 1000000};
        order = stopping.on_market(held);
        check(order && order->quantity_atoms == 200000 && stopping.last_signal() == strategy::Signal::take_profit);
        stop_config.stop_loss_ppm = 0;
        strategy::Threshold unprotected(stop_config);
        held.time.ordinal = 1; held.bid = Level{5000, 1000000}; held.ask = Level{5100, 1000000};
        check(!unprotected.on_market(held)); // Without a stop the policy holds at any loss.
        threw = false;
        stop_config.stop_loss_ppm = 1000000;
        try { strategy::Threshold bad(stop_config); } catch (const std::runtime_error&) { threw = true; }
        check(threw);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
