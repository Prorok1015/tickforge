#include "commands.hpp"
#include "paper_scenario.hpp"
#include "paper_reporter.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/core/amounts.hpp>
#include <tickforge/execution/simulator.hpp>
#include <tickforge/console/console.hpp>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>

namespace tickforge::app {
void paper_session(std::string_view session_path, std::string_view scenario_path, bool automatic)
{
    const auto scenario = load_paper_scenario(scenario_path);
    if (automatic != scenario.strategy.has_value())
        throw std::runtime_error("strategy command requires a threshold row; paper command requires scripted orders");
    std::optional<strategy::Threshold> algorithm;
    if (scenario.strategy) algorithm.emplace(*scenario.strategy);
    std::ifstream input{std::string(session_path)};
    if (!input) throw std::runtime_error(std::format("cannot open session '{}'", session_path));
    accounting::Account account(scenario.initial_quote_atoms, scenario.initial_base_atoms, scenario.initial_base_cost_atoms);
    execution::Simulator simulator(scenario.config);
    execution::OrderManager manager(account, simulator, scenario.config);
    std::size_t next = 0, rejected = 0, fills = 0;
    std::int64_t fees = 0;
    std::optional<Mark> mark;
    paper_header(session_path, scenario_path, scenario);
    if (algorithm) write_console(std::format(
        "Strategy  | threshold | buy ask <= {} USDT | sell bid >= {} USDT\n"
        "Decision  | once per event, confirmed inventory, at most one market request\n\n",
        price_text(scenario.strategy->buy_price_atoms), price_text(scenario.strategy->sell_price_atoms)));
    const auto submit = [&](const MarketOrder& order) {
        const auto report = manager.submit(order);
        paper_order(report);
        if (report.result.status == OrderStatus::rejected) ++rejected;
        fills += report.result.fills.size();
        for (const auto& fill : report.result.fills) fees = amount_add(fees, fill.fee_quote_atoms);
        ++next;
    };
    replay(input, [&](const MarketEvent& event) {
        simulator.on_market_event(event);
        paper_market(event, simulator.available());
        // Recordings end with a shutdown reset, so keep the last trusted bid for valuation.
        if (const auto bid = simulator.book().best_bid(); bid && simulator.available())
            mark = Mark{bid->price_atoms, event.time.ordinal};
        if (algorithm) {
            const auto order = algorithm->on_market({event.time, simulator.available(),
                simulator.executable_level(Side::sell), simulator.executable_level(Side::buy),
                account.balances(), account.position()});
            if (order) {
                write_console(std::format("Signal    | event #{} | {} threshold reached\n", event.time.ordinal,
                    order->side == Side::buy ? "BUY ask" : "SELL bid"));
                submit(*order);
            }
            return;
        }
        while (next < scenario.orders.size() && scenario.orders[next].after_ordinal == event.time.ordinal) {
            submit(scenario.orders[next].order);
        }
    });
    if (!automatic && next != scenario.orders.size()) throw std::runtime_error("scenario references an event beyond the recorded session");
    paper_summary(account, next, rejected, fills, fees, mark);
}
} // namespace tickforge::app
