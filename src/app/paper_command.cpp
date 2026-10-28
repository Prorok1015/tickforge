#include "commands.hpp"
#include "paper_scenario.hpp"
#include "paper_reporter.hpp"
#include "paper_export.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/core/amounts.hpp>
#include <tickforge/accounting/valuation.hpp>
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

void paper_session(const PaperOptions& options)
{
    const std::string_view session_path = options.session, scenario_path = options.config;
    const std::string_view report_prefix = options.report_prefix;
    const bool automatic = options.automatic;
    const auto scenario = load_paper_scenario(scenario_path, options.overrides);
    if (automatic != scenario.strategy.has_value())
        throw std::runtime_error("strategy command requires a [strategy] section; paper command requires [[orders]] instead");
    std::optional<strategy::Threshold> algorithm;
    if (scenario.strategy) algorithm.emplace(*scenario.strategy);
    std::ifstream input{std::string(session_path)};
    if (!input) throw std::runtime_error(std::format("cannot open session '{}'", session_path));
    accounting::Account account(scenario.initial_quote_atoms, scenario.initial_base_atoms, scenario.initial_base_cost_atoms);
    execution::Simulator simulator(scenario.config);
    // Initial equity matches the PnL definition: starting USDT plus the declared BTC cost.
    execution::RiskGuard risk(scenario.risk, amount_add(scenario.initial_quote_atoms, scenario.initial_base_cost_atoms));
    execution::OrderManager manager(account, simulator, scenario.config, &risk);
    PaperTotals totals;
    std::size_t next = 0, next_cancel = 0;
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
    const auto record = [&](const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                            bool first) {
        paper_order(report, first);
        if (exporter) exporter->order(submitted, signal, report);
        totals.fills += report.result.fills.size();
        for (const auto& fill : report.result.fills) {
            totals.fees = amount_add(totals.fees, fill.fee_quote_atoms);
            if (fill.liquidity == Liquidity::maker) ++totals.maker_fills;
        }
        if (!report.final) return;
        if (report.result.status == OrderStatus::rejected) ++totals.rejected;
        if (report.interruption == execution::Interruption::no_trusted_book) ++totals.cancelled_no_book;
        if (report.interruption == execution::Interruption::session_ended) ++totals.cancelled_at_end;
    };
    // Submission time and signal of accepted orders until their final report arrives.
    struct Submitted { EventTime time; std::string_view signal; bool reported = false; };
    std::map<std::int64_t, Submitted> in_flight;
    const auto collect = [&] {
        for (const auto& report : manager.collect()) {
            const auto found = in_flight.find(report.order.id);
            if (found == in_flight.end()) throw std::runtime_error("report for an order this run did not submit");
            record(report, found->second.time, found->second.signal, !found->second.reported);
            found->second.reported = true;
            if (report.final) in_flight.erase(found);
        }
    };
    // Prints a risk state change once and remembers when it happened for the report.
    const auto note_risk = [&](const EventTime& time) {
        if (risk.state() == totals.risk_state) return;
        totals.risk_state = risk.state();
        totals.risk_event = time.ordinal;
        write_console(std::format("Risk      | event #{} | {} after {} | equity={} USDT | start={} USDT | peak={} USDT\n",
            time.ordinal, risk.state() == execution::TradingState::halted ? "HALTED: no orders" : "REDUCE-ONLY: sells only",
            execution::trigger_name(risk.trigger()),
            risk.trigger_equity() ? money_text(*risk.trigger_equity()) : std::string("unknown"),
            money_text(amount_add(scenario.initial_quote_atoms, scenario.initial_base_cost_atoms)),
            money_text(risk.peak_equity())));
        // Resting orders the new state forbids are withdrawn: buys in reduce-only, all when halted.
        const auto cancelled = manager.cancel_open(risk.state() == execution::TradingState::halted
            ? std::nullopt : std::optional<Side>(Side::buy));
        if (cancelled != 0) write_console(std::format("Risk      | cancel requested for {} resting order(s)\n", cancelled));
    };
    const auto submit = [&](const MarketEvent& event, const Order& order, std::string_view signal) {
        ++totals.requests;
        const auto rejected = manager.submit(order);
        note_risk(event.time);
        if (rejected) {
            record(*rejected, event.time, signal, true);
        } else {
            in_flight.emplace(order.id, Submitted{event.time, signal});
            if (scenario.config.order_latency_ns != 0 || scenario.config.report_latency_ns != 0)
                write_console(std::format("Sent      | order #{} | elapsed={} ns | funds reserved; result pending\n",
                    order.id, event.time.receive_elapsed_ns));
        }
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
        // Risk is judged only on a trusted current valuation; a flat account needs no price.
        std::optional<std::int64_t> equity;
        if (account.balances().base_total == 0) equity = account.balances().quote_total;
        else if (simulator.available() && mark && mark->ordinal == event.time.ordinal)
            equity = accounting::value_at(account, mark->price_atoms).equity_quote;
        risk.observe(event.time.receive_elapsed_ns, equity);
        note_risk(event.time);
        if (algorithm) {
            const auto order = algorithm->on_market({event.time, simulator.available(),
                simulator.executable_level(Side::sell), simulator.executable_level(Side::buy),
                account.balances(), account.position(), risk.buys_allowed(), risk.sells_allowed()});
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
        // Scripted cancels follow the orders of the same event.
        while (next_cancel < scenario.cancels.size() && scenario.cancels[next_cancel].after_ordinal == event.time.ordinal) {
            const auto id = scenario.cancels[next_cancel++].order_id;
            write_console(manager.cancel(id) ? std::format("Cancel    | order #{} | requested\n", id)
                                             : std::format("Cancel    | order #{} | nothing open to cancel\n", id));
            collect();
        }
    });
    if (!automatic && (next != scenario.orders.size() || next_cancel != scenario.cancels.size()))
        throw std::runtime_error("scenario references an event beyond the recorded session");
    // Orders still in flight cannot execute without market data; executed ones still report.
    simulator.finish();
    collect();
    if (manager.in_flight() != 0) throw std::runtime_error("orders remain unresolved after the session");
    paper_summary(account, totals.requests, totals.rejected, totals.fills, totals.fees, mark);
    if (exporter) {
        exporter->complete(automatic ? "strategy" : "paper", session_path, scenario_path, scenario, account, totals, mark, risk);
        write_console(std::format("Report    | {}\n", exporter->summary_path()));
    }
}
} // namespace tickforge::app
