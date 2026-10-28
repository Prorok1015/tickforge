#pragma once
#include "paper_scenario.hpp"
#include "paper_export.hpp"
#include <tickforge/engine/session.hpp>
#include <tickforge/strategy/strategy.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace tickforge::app {
// The policy named by [strategy] type, or null for a scripted [[orders]] run.
std::unique_ptr<strategy::Strategy> make_strategy(const PaperScenario& scenario);
// Strategy, stop-loss, cooldown, and decision lines for the run header.
std::string strategy_header(const PaperScenario& scenario);

// Console journal, totals, markouts, and optional CSV reports of one session.
class PaperJournal final : public engine::SessionObserver {
public:
    struct Output {
        bool journal = true;      // orders, fills, signals, risk, notices on the console
        bool market_lines = true; // one line per market event (offline runs only)
    };
    PaperJournal(const PaperScenario& scenario, std::optional<PaperExport>& exporter, Output output);
    void attach(const engine::Session& session) { session_ = &session; }
    PaperTotals totals;
    Markouts markouts;

    void on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask) override;
    void on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                   bool first) override;
    void on_request(const EventTime& time, const Order& order) override;
    void on_accepted(const EventTime& time, const Order& order) override;
    void on_risk(const EventTime& time, std::size_t cancelled) override;
    void on_signal(const EventTime& time, const strategy::Place& place) override;
    void on_cancel(const EventTime& time, std::int64_t order_id, bool requested) override;
    void on_notice(const EventTime& time, std::string_view text) override;
    // After Session::finish(): final balances, PnL, fill evidence, and the summary report.
    void complete(std::string_view command, std::string_view session_path, std::string_view scenario_path);
private:
    const PaperScenario& scenario_;
    std::optional<PaperExport>& exporter_;
    Output output_;
    const engine::Session* session_ = nullptr;
};
} // namespace tickforge::app
