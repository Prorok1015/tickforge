#include "commands.hpp"
#include "paper_scenario.hpp"
#include "paper_reporter.hpp"
#include "paper_export.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/core/amounts.hpp>
#include <tickforge/engine/session.hpp>
#include <tickforge/strategy/threshold_maker.hpp>
#include <tickforge/console/console.hpp>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace tickforge::app {
namespace {
// Console journal, totals, markouts, and optional CSV reports of one session.
class PaperJournal final : public engine::SessionObserver {
public:
    PaperJournal(const PaperScenario& scenario, std::optional<PaperExport>& exporter)
        : scenario_(scenario), exporter_(exporter) {}
    void attach(const engine::Session& session) { session_ = &session; }
    PaperTotals totals;
    Markouts markouts;

    void on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask) override
    {
        ++totals.events;
        // Report-only: later prices measure earlier fills and never reach the strategy.
        if (available) markouts.observe(event.time.receive_elapsed_ns, bid, ask);
        for (const auto& row : markouts.take_done())
            if (exporter_) exporter_->markout(row);
        paper_market(event, available);
    }
    void on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                   bool first) override
    {
        paper_order(report, first);
        if (exporter_) exporter_->order(submitted, signal, report);
        totals.fills += report.result.fills.size();
        for (const auto& fill : report.result.fills) {
            totals.fees = amount_add(totals.fees, fill.fee_quote_atoms);
            if (fill.liquidity == Liquidity::maker) ++totals.maker_fills;
            markouts.fill(fill, report.executed_ns);
        }
        for (const auto& diagnostic : report.diagnostics) totals.fill_stats.add(diagnostic);
        if (!report.final) return;
        if (report.result.status == OrderStatus::rejected) ++totals.rejected;
        if (report.interruption == execution::Interruption::no_trusted_book) ++totals.cancelled_no_book;
        if (report.interruption == execution::Interruption::session_ended) ++totals.cancelled_at_end;
    }
    void on_request(const EventTime&, const Order&) override { ++totals.requests; }
    void on_accepted(const EventTime& time, const Order& order) override
    {
        if (scenario_.config.order_latency_ns != 0 || scenario_.config.report_latency_ns != 0)
            write_console(std::format("Sent      | order #{} | elapsed={} ns | funds reserved; result pending\n",
                order.id, time.receive_elapsed_ns));
    }
    void on_risk(const EventTime& time, std::size_t cancelled) override
    {
        const auto& risk = session_->risk();
        totals.risk_state = risk.state();
        totals.risk_event = time.ordinal;
        write_console(std::format("Risk      | event #{} | {} after {} | equity={} USDT | start={} USDT | peak={} USDT\n",
            time.ordinal, risk.state() == execution::TradingState::halted ? "HALTED: no orders" : "REDUCE-ONLY: sells only",
            execution::trigger_name(risk.trigger()),
            risk.trigger_equity() ? money_text(*risk.trigger_equity()) : std::string("unknown"),
            money_text(session_->initial_equity()), money_text(risk.peak_equity())));
        if (cancelled != 0) write_console(std::format("Risk      | cancel requested for {} resting order(s)\n", cancelled));
    }
    void on_signal(const EventTime& time, const strategy::Place& place) override
    {
        const bool resting = place.order.type == OrderType::limit;
        write_console(std::format("Signal    | event #{} | {}\n", time.ordinal,
            place.signal == strategy::Signal::stop_loss ? "STOP-LOSS position value fell below the limit"
            : place.signal == strategy::Signal::buy ? (resting ? "BUY limit bid placed" : "BUY ask threshold reached")
            : (resting ? "SELL limit ask placed" : "SELL bid threshold reached")));
    }
    void on_cancel(const EventTime& time, std::int64_t order_id, bool requested) override
    {
        write_console(std::format("Cancel    | event #{} | order #{} | {}\n", time.ordinal, order_id,
            requested ? "requested by the strategy" : "nothing open to cancel"));
    }
    void on_notice(const EventTime& time, std::string_view text) override
    {
        write_console(std::format("Notice    | event #{} | {}\n", time.ordinal, text));
    }
private:
    const PaperScenario& scenario_;
    std::optional<PaperExport>& exporter_;
    const engine::Session* session_ = nullptr;
};
} // namespace

void paper_session(const PaperOptions& options)
{
    const std::string_view session_path = options.session, scenario_path = options.config;
    const std::string_view report_prefix = options.report_prefix;
    const bool automatic = options.automatic;
    const auto scenario = load_paper_scenario(scenario_path, options.overrides);
    if (automatic != scenario.strategy.has_value())
        throw std::runtime_error("strategy command requires a [strategy] section; paper command requires [[orders]] instead");
    std::unique_ptr<strategy::Strategy> algorithm;
    if (scenario.strategy_type == "threshold_maker") algorithm = std::make_unique<strategy::ThresholdMaker>(*scenario.strategy);
    else if (scenario.strategy) algorithm = std::make_unique<strategy::Threshold>(*scenario.strategy);
    std::ifstream input{std::string(session_path)};
    if (!input) throw std::runtime_error(std::format("cannot open session '{}'", session_path));
    // Created before the run so that an existing report fails fast and is never overwritten.
    std::optional<PaperExport> exporter;
    if (!report_prefix.empty()) exporter.emplace(report_prefix);
    PaperJournal journal(scenario, exporter);
    engine::Session session({scenario.config, scenario.risk, scenario.initial_quote_atoms, scenario.initial_base_atoms,
                             scenario.initial_base_cost_atoms}, std::move(algorithm), journal);
    journal.attach(session);
    std::size_t next = 0, next_cancel = 0;
    paper_header(session_path, scenario_path, scenario);
    const bool maker = scenario.strategy_type == "threshold_maker";
    if (automatic) write_console(std::format(
        "{}"
        "Stop-loss | {}\n"
        "Cooldown  | {}\n"
        "Decision  | once per event, confirmed inventory and open orders\n\n",
        maker ? std::format("Strategy  | threshold_maker | post-only bid at <= {} USDT | post-only ask at >= {} USDT, "
                            "never below cost after the maker fee\n",
                            price_text(scenario.strategy->buy_price_atoms), price_text(scenario.strategy->sell_price_atoms))
              : std::format("Strategy  | threshold | buy ask <= {} USDT | sell bid >= {} USDT, never below fee-adjusted cost\n",
                            price_text(scenario.strategy->buy_price_atoms), price_text(scenario.strategy->sell_price_atoms)),
        scenario.strategy->stop_loss_ppm == 0 ? std::string("off")
            : std::format("sell at market when position value is {} ppm below cost", scenario.strategy->stop_loss_ppm),
        scenario.strategy->cooldown_ns == 0 ? std::string("off")
            : std::format("no buys for {} ns after a stop-loss closes the position", scenario.strategy->cooldown_ns)));
    replay(input, [&](const MarketEvent& event) {
        session.on_event(event);
        if (automatic) return;
        while (next < scenario.orders.size() && scenario.orders[next].after_ordinal == event.time.ordinal)
            session.submit(scenario.orders[next++].order, "script");
        // Scripted cancels follow the orders of the same event.
        while (next_cancel < scenario.cancels.size() && scenario.cancels[next_cancel].after_ordinal == event.time.ordinal) {
            const auto id = scenario.cancels[next_cancel++].order_id;
            write_console(session.cancel(id) ? std::format("Cancel    | order #{} | requested\n", id)
                                             : std::format("Cancel    | order #{} | nothing open to cancel\n", id));
        }
    });
    if (!automatic && (next != scenario.orders.size() || next_cancel != scenario.cancels.size()))
        throw std::runtime_error("scenario references an event beyond the recorded session");
    session.finish();
    auto& totals = journal.totals;
    paper_summary(session.account(), totals.requests, totals.rejected, totals.fills, totals.fees, session.mark());
    paper_fill_diagnostics(totals.fill_stats, journal.markouts);
    for (const auto& row : journal.markouts.take_done(true))
        if (exporter) exporter->markout(row);
    if (exporter) {
        exporter->complete(automatic ? "strategy" : "paper", session_path, scenario_path, scenario, session.account(),
                           totals, journal.markouts, session.mark(), session.risk());
        write_console(std::format("Report    | {}\n", exporter->summary_path()));
    }
}
} // namespace tickforge::app
