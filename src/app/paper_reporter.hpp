#pragma once
#include "paper_scenario.hpp"
#include "paper_diagnostics.hpp"
#include <tickforge/core/events.hpp>
#include <tickforge/execution/order_manager.hpp>
#include <tickforge/engine/session.hpp>

#include <optional>

namespace tickforge::app {
using Mark = engine::Mark;

// 1e-8 USDT atoms as a fixed eight-decimal amount.
std::string money_text(std::int64_t atoms);
// Signed 1e-8 USDT atoms with an explicit sign.
std::string signed_money_text(std::int64_t atoms);
// "k=v; k=v" in command-line order, or "none".
std::string overrides_text(const std::vector<std::string>& overrides);
void paper_header(std::string_view session, std::string_view scenario, const PaperScenario& config);
void paper_market(const MarketEvent& event, bool available);
// first: the order's first report (shows the request and its reservation).
void paper_order(const execution::OrderReport& report, bool first = true);
void paper_summary(const accounting::Account& account, std::size_t orders, std::size_t rejected,
                   std::size_t fills, std::int64_t fees, std::optional<Mark> mark);
// How far simulated fills can be trusted, and what the market did right after them.
void paper_fill_diagnostics(const FillStats& stats, const Markouts& markouts);
}
