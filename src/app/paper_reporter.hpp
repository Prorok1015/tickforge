#pragma once
#include "paper_scenario.hpp"
#include <tickforge/core/events.hpp>
#include <tickforge/execution/order_manager.hpp>

#include <optional>

namespace tickforge::app {
// Last best bid observed while execution was available: the valuation price.
struct Mark { std::int64_t price_atoms; std::int64_t ordinal; };

void paper_header(std::string_view session, std::string_view scenario, const PaperScenario& config);
void paper_market(const MarketEvent& event, bool available);
void paper_order(const execution::OrderReport& report);
void paper_summary(const accounting::Account& account, std::size_t orders, std::size_t rejected,
                   std::size_t fills, std::int64_t fees, std::optional<Mark> mark);
}
