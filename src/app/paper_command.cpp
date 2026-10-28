#include "commands.hpp"
#include "paper_scenario.hpp"
#include "paper_reporter.hpp"
#include "paper_export.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/core/amounts.hpp>
#include <tickforge/execution/simulator.hpp>
#include <tickforge/console/console.hpp>
#include <format>
#include <fstream>
#include <map>
#include <utility>
#include <stdexcept>
#include <string>

namespace tickforge::app {
namespace {
std::string_view signal_name(strategy::Signal signal)
{
    switch (signal) {
    case strategy::Signal::buy: return "buy";
    case strategy::Signal::take_profit: return "take_profit";
    case strategy::Signal::stop_loss: return "stop_loss";
    case strategy::Signal::none: break;
    }
    return "none";
}
} // namespace

void paper_session(std::string_view session_path, std::string_view scenario_path, bool automatic,
                   std::string_view report_prefix)
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
    PaperTotals totals;
    std::size_t next = 0;
    std::optional<Mark> mark;
    // Created before the run so that an existing report fails fast and is never overwritten.
    std::optional<PaperExport> exporter;
    if (!report_prefix.empty()) exporter.emplace(report_prefix);
    paper_header(session_path, scenario_path, scenario);
    if (algorithm) write_console(std::format(
        "Strategy  | threshold | buy ask <= {} USDT | sell bid >= {} USDT, never below fee-adjusted cost\n"
        "Stop-loss | {}\n"
        "Cooldown  | {}\n"
        "Decision  | once per event, confirmed inventory, at most one market request\n\n",
        price_text(scenario.strategy->buy_price_atoms), price_text(scenario.strategy->sell_price_atoms),
        scenario.strategy->stop_loss_ppm == 0 ? std::string("off")
            : std::format("sell at market when position value is {} ppm below cost", scenario.strategy->stop_loss_ppm),
        scenario.strategy->cooldown_ns == 0 ? std::string("off")
            : std::format("no buys for {} ns after a stop-loss closes the position", scenario.strategy->cooldown_ns)));
    const auto record = [&](const execution::OrderReport& report, const EventTime& submitted, std::string_view signal) {
        paper_order(report);
        if (exporter) exporter->order(submitted, signal, report);
        if (report.result.status == OrderStatus::rejected) ++totals.rejected;
        totals.fills += report.result.fills.size();
        for (const auto& fill : report.result.fills) totals.fees = amount_add(totals.fees, fill.fee_quote_atoms);
    };
    // Submission time and signal of accepted orders until their reports arrive.
    std::map<std::int64_t, std::pair<EventTime, std::string_view>> in_flight;
    const auto collect = [&] {
        for (const auto& report : manager.collect()) {
            const auto found = in_flight.find(report.order.id);
            if (found == in_flight.end()) throw std::runtime_error("report for an order this run did not submit");
            record(report, found->second.first, found->second.second);
            in_flight.erase(found);
        }
    };
    const auto submit = [&](const MarketEvent& event, const MarketOrder& order, std::string_view signal) {
        ++totals.requests;
        if (const auto rejected = manager.submit(order)) record(*rejected, event.time, signal);
        else in_flight.emplace(order.id, std::pair{event.time, signal});
        collect();
    };
    replay(input, [&](const MarketEvent& event) {
        ++totals.events;
        simulator.on_market_event(event);
        collect();
        paper_market(event, simulator.available());
        // Recordings end with a shutdown reset, so keep the last trusted bid for valuation.
        if (const auto bid = simulator.book().best_bid(); bid && simulator.available())
            mark = Mark{bid->price_atoms, event.time.ordinal};
        if (algorithm) {
            const auto order = algorithm->on_market({event.time, simulator.available(),
                simulator.executable_level(Side::sell), simulator.executable_level(Side::buy),
                account.balances(), account.position()});
            if (const auto until = algorithm->started_cooldown_until())
                write_console(std::format("Cooldown  | event #{} | stop-loss closed the position; no buys until elapsed={} ns\n",
                    event.time.ordinal, *until));
            if (order) {
                const auto signal = algorithm->last_signal();
                write_console(std::format("Signal    | event #{} | {}\n", event.time.ordinal,
                    signal == strategy::Signal::buy ? "BUY ask threshold reached"
                    : signal == strategy::Signal::stop_loss ? "STOP-LOSS position value fell below the limit"
                    : "SELL bid threshold reached"));
                submit(event, *order, signal_name(signal));
            }
            return;
        }
        while (next < scenario.orders.size() && scenario.orders[next].after_ordinal == event.time.ordinal) {
            submit(event, scenario.orders[next++].order, "script");
        }
    });
    if (!automatic && next != scenario.orders.size()) throw std::runtime_error("scenario references an event beyond the recorded session");
    paper_summary(account, totals.requests, totals.rejected, totals.fills, totals.fees, mark);
    if (exporter) {
        exporter->complete(automatic ? "strategy" : "paper", session_path, scenario_path, scenario, account, totals, mark);
        write_console(std::format("Report    | {}\n", exporter->summary_path()));
    }
}
} // namespace tickforge::app
