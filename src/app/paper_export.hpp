#pragma once
#include "paper_reporter.hpp"
#include "paper_diagnostics.hpp"
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace tickforge::app {
struct PaperTotals {
    std::int64_t events = 0;
    std::size_t requests = 0;
    std::size_t rejected = 0;
    std::size_t fills = 0;
    std::int64_t fees = 0;
    std::size_t cancelled_no_book = 0; // accepted, but no trusted book at execution time
    std::size_t cancelled_at_end = 0;  // accepted, but market data ended first
    std::size_t maker_fills = 0;       // fills of our resting limit orders
    FillStats fill_stats;
    execution::TradingState risk_state = execution::TradingState::normal;
    std::optional<std::int64_t> risk_event; // ordinal of the event that changed the risk state
};

// Machine-readable experiment output in integer atoms. <prefix>-orders.csv,
// <prefix>-fills.csv and <prefix>-markouts.csv grow during the run; <prefix>-summary.csv
// is created last and ends with "status,complete", so only that row confirms a completed
// experiment. All files must be new: an existing experiment is never overwritten.
class PaperExport {
public:
    explicit PaperExport(std::string_view prefix);
    void order(const EventTime& submitted, std::string_view signal, const execution::OrderReport& report);
    void markout(const MarkoutRow& row);
    void complete(std::string_view command, std::string_view session, std::string_view scenario_path,
                  const PaperScenario& scenario, const accounting::Account& account, const PaperTotals& totals,
                  const Markouts& markouts, std::optional<Mark> mark, const execution::RiskGuard& risk);
    const std::string& summary_path() const { return summary_path_; }
private:
    struct Close { void operator()(std::FILE* file) const { std::fclose(file); } };
    using File = std::unique_ptr<std::FILE, Close>;
    std::string orders_path_, fills_path_, markouts_path_, summary_path_;
    File orders_, fills_, markouts_;
    std::map<std::int64_t, std::size_t> fill_counts_; // fills per order until its final report
};
} // namespace tickforge::app
