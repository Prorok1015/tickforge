#include <tickforge/engine/session.hpp>
#include <tickforge/strategy/threshold.hpp>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace tickforge;

namespace {
void check(bool ok, const char* what)
{
    if (!ok) throw std::runtime_error(std::string("engine contract failed: ") + what);
}

// Records the callbacks a session makes, as text, to compare runs exactly.
class Trace final : public engine::SessionObserver {
public:
    std::vector<std::string> lines;
    void on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level>) override
    {
        lines.push_back("market " + std::to_string(event.time.ordinal) + (available ? " available" : " blocked") +
                        (bid ? " bid " + std::to_string(bid->price_atoms) : ""));
    }
    void on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                   bool first) override
    {
        lines.push_back("report " + std::to_string(report.order.id) + " " + std::string(signal) + " at " +
                        std::to_string(submitted.ordinal) + (first ? " first" : "") + (report.final ? " final" : "") +
                        " fills " + std::to_string(report.result.fills.size()));
    }
    void on_signal(const EventTime& time, const strategy::Place& place) override
    {
        lines.push_back("signal " + std::to_string(time.ordinal) + " order " + std::to_string(place.order.id));
    }
};

template<class Data> MarketEvent event(std::int64_t ordinal, Data data)
{
    return {{ordinal, 0, 0, ordinal * 1000000}, std::move(data)};
}

engine::SessionConfig config()
{
    engine::SessionConfig settings;
    settings.execution.quantity_step_atoms = 1000;
    settings.initial_quote_atoms = 100000000000; // 1000 USDT
    return settings;
}

std::vector<MarketEvent> market()
{
    return {event(1, BookSnapshot{1, {{9900, 1000000}}, {{10000, 1000000}}}),
            event(2, BookUpdate{2, 2, {{9900, 0}, {10300, 1000000}}, {{10000, 0}, {10400, 1000000}}}),
            event(3, BookUpdate{3, 3, {{10300, 500000}}, {}})};
}

std::vector<std::string> run_strategy()
{
    Trace trace;
    // Buy at <= 100.00, sell at >= 102.00, 0.002 BTC, budget 10 USDT, taker 0.1%.
    engine::Session session(config(),
        std::make_unique<strategy::Threshold>(strategy::ThresholdConfig{10000, 10200, 200000, 1000000000, 1000, 1000}),
        trace);
    for (const auto& item : market()) session.on_event(item);
    session.finish();
    check(session.account().balances().base_total == 0 && session.account().position().realized_pnl_quote > 0,
          "the strategy buys at 100.00 and sells at 103.00 for a profit");
    return trace.lines;
}

void strategy_loop()
{
    const auto lines = run_strategy();
    const std::vector<std::string> expected{
        "market 1 available bid 9900", "signal 1 order 1", "report 1 buy at 1 first final fills 1",
        "market 2 available bid 10300", "signal 2 order 2", "report 2 take_profit at 2 first final fills 1",
        "market 3 available bid 10300"};
    check(lines == expected, "events, decisions, and reports follow the shared loop order");
    check(run_strategy() == lines, "the same events reproduce the same run");
}

void scripted_orders()
{
    Trace trace;
    engine::Session session(config(), nullptr, trace);
    bool refused = false;
    try { session.submit({1, Side::buy, 100000, 1000000000}, "script"); } catch (const std::runtime_error&) { refused = true; }
    check(refused, "an order needs a market event first");
    session.on_event(market()[0]);
    session.submit({1, Side::buy, 100000, 1000000000}, "script");
    check(session.account().balances().base_total == 100000 && trace.lines.back() == "report 1 script at 1 first final fills 1",
          "a scripted order executes at the last event");
    check(!session.cancel(1), "a filled order has nothing to cancel");
    session.finish();
    check(session.mark() && session.mark()->price_atoms == 9900 && session.initial_equity() == 100000000000,
          "the mark is the last trusted bid; equity starts at the initial quote");
}
} // namespace

int main()
{
    try {
        strategy_loop();
        scripted_orders();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
