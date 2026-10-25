#include <tickforge/strategy/threshold.hpp>
#include <stdexcept>
#include <cstdio>

using namespace tickforge;
void check(bool ok) { if (!ok) throw std::runtime_error("strategy contract failed"); }
int main()
{
    try {
        strategy::Threshold algorithm({10000, 10200, 200000, 1000000000, 1000});
        strategy::Strategy& interface = algorithm;
        strategy::View view{{1, 0, 0, 0}, false, Level{9900, 1000000}, Level{10000, 100000},
            {10000000000, 0, 0, 0}};
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
        try { strategy::Threshold bad({10200, 10000, 1, 1, 1000}); } catch (const std::runtime_error&) { threw = true; }
        check(threw);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
