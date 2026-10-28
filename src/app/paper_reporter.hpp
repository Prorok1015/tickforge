#pragma once
#include "paper_scenario.hpp"
#include <tickforge/core/events.hpp>
#include <tickforge/execution/order_manager.hpp>

#include <optional>

namespace tickforge::app {
// Last best bid observed while execution was available: the valuation price.
struct Mark { std::int64_t price_atoms; std::int64_t ordinal; };

// 1e-8 USDT atoms as a fixed eight-decimal amount.
std::string money_text(std::int64_t atoms);
// "k=v; k=v" in command-line order, or "none".
std::string overrides_text(const std::vector<std::string>& overrides);
void paper_header(std::string_view session, std::string_view scenario, const PaperScenario& config);
void paper_market(const MarketEvent& event, bool available);
// first: the order's first report (shows the request and its reservation).
void paper_order(const execution::OrderReport& report, bool first = true);
void paper_summary(const accounting::Account& account, std::size_t orders, std::size_t rejected,
                   std::size_t fills, std::int64_t fees, std::optional<Mark> mark);
}
