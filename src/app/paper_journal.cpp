#include "paper_journal.hpp"
#include "paper_reporter.hpp"
#include <tickforge/core/amounts.hpp>
#include <tickforge/strategy/threshold_maker.hpp>
#include <tickforge/console/console.hpp>
#include <format>

namespace tickforge::app {
std::unique_ptr<strategy::Strategy> make_strategy(const PaperScenario& scenario)
{
    if (!scenario.strategy) return nullptr;
    if (scenario.strategy_type == "threshold_maker") return std::make_unique<strategy::ThresholdMaker>(*scenario.strategy);
    return std::make_unique<strategy::Threshold>(*scenario.strategy);
}

std::string strategy_header(const PaperScenario& scenario)
{
    const auto& s = *scenario.strategy;
    return std::format(
        "{}"
        "Stop-loss | {}\n"
        "Cooldown  | {}\n"
        "Decision  | once per event, confirmed inventory and open orders\n\n",
        scenario.strategy_type == "threshold_maker"
            ? std::format("Strategy  | threshold_maker | post-only bid at <= {} USDT | post-only ask at >= {} USDT, "
                          "never below cost after the maker fee\n", price_text(s.buy_price_atoms), price_text(s.sell_price_atoms))
            : std::format("Strategy  | threshold | buy ask <= {} USDT | sell bid >= {} USDT, never below fee-adjusted cost\n",
                          price_text(s.buy_price_atoms), price_text(s.sell_price_atoms)),
        s.stop_loss_ppm == 0 ? std::string("off")
            : std::format("sell at market when position value is {} ppm below cost", s.stop_loss_ppm),
        s.cooldown_ns == 0 ? std::string("off")
            : std::format("no buys for {} ns after a stop-loss closes the position", s.cooldown_ns));
}

PaperJournal::PaperJournal(const PaperScenario& scenario, std::optional<PaperExport>& exporter, Output output)
    : scenario_(scenario), exporter_(exporter), output_(output)
{
}

void PaperJournal::on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask)
{
    ++totals.events;
    // Report-only: later prices measure earlier fills and never reach the strategy.
    if (available) markouts.observe(event.time.receive_elapsed_ns, bid, ask);
    for (const auto& row : markouts.take_done())
        if (exporter_) exporter_->markout(row);
    if (output_.journal && output_.market_lines) paper_market(event, available);
}

void PaperJournal::on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                             bool first)
{
    if (output_.journal) paper_order(report, first);
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

void PaperJournal::on_request(const EventTime&, const Order&) { ++totals.requests; }

void PaperJournal::on_accepted(const EventTime& time, const Order& order)
{
    if (output_.journal && (scenario_.config.order_latency_ns != 0 || scenario_.config.report_latency_ns != 0))
        write_console(std::format("Sent      | order #{} | elapsed={} ns | funds reserved; result pending\n",
            order.id, time.receive_elapsed_ns));
}

void PaperJournal::on_risk(const EventTime& time, std::size_t cancelled)
{
    const auto& risk = session_->risk();
    totals.risk_state = risk.state();
    totals.risk_event = time.ordinal;
    if (!output_.journal) return;
    write_console(std::format("Risk      | event #{} | {} after {} | equity={} USDT | start={} USDT | peak={} USDT\n",
        time.ordinal, risk.state() == execution::TradingState::halted ? "HALTED: no orders" : "REDUCE-ONLY: sells only",
        execution::trigger_name(risk.trigger()),
        risk.trigger_equity() ? money_text(*risk.trigger_equity()) : std::string("unknown"),
        money_text(session_->initial_equity()), money_text(risk.peak_equity())));
    if (cancelled != 0) write_console(std::format("Risk      | cancel requested for {} resting order(s)\n", cancelled));
}

void PaperJournal::on_signal(const EventTime& time, const strategy::Place& place)
{
    if (!output_.journal) return;
    const bool resting = place.order.type == OrderType::limit;
    write_console(std::format("Signal    | event #{} | {}\n", time.ordinal,
        place.signal == strategy::Signal::stop_loss ? "STOP-LOSS position value fell below the limit"
        : place.signal == strategy::Signal::buy ? (resting ? "BUY limit bid placed" : "BUY ask threshold reached")
        : (resting ? "SELL limit ask placed" : "SELL bid threshold reached")));
}

void PaperJournal::on_cancel(const EventTime& time, std::int64_t order_id, bool requested)
{
    if (!output_.journal) return;
    write_console(std::format("Cancel    | event #{} | order #{} | {}\n", time.ordinal, order_id,
        requested ? "requested by the strategy" : "nothing open to cancel"));
}

void PaperJournal::on_notice(const EventTime& time, std::string_view text)
{
    if (output_.journal) write_console(std::format("Notice    | event #{} | {}\n", time.ordinal, text));
}

void PaperJournal::complete(std::string_view command, std::string_view session_path, std::string_view scenario_path)
{
    const auto& session = *session_;
    paper_summary(session.account(), totals.requests, totals.rejected, totals.fills, totals.fees, session.mark());
    paper_fill_diagnostics(totals.fill_stats, markouts);
    for (const auto& row : markouts.take_done(true))
        if (exporter_) exporter_->markout(row);
    if (exporter_) {
        exporter_->complete(command, session_path, scenario_path, scenario_, session.account(), totals, markouts,
                            session.mark(), session.risk());
        write_console(std::format("Report    | {}\n", exporter_->summary_path()));
    }
}
} // namespace tickforge::app
