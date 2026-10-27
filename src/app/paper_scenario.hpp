#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/strategy/threshold.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace tickforge::app {
struct ScriptOrder { std::int64_t after_ordinal; MarketOrder order; };
struct PaperScenario {
    std::int64_t initial_quote_atoms;
    std::int64_t initial_base_atoms;
    std::int64_t initial_base_cost_atoms; // optional field; required with initial BTC
    execution::PaperConfig config;
    std::vector<ScriptOrder> orders;
    std::optional<strategy::ThresholdConfig> strategy;
};
PaperScenario load_paper_scenario(std::string_view path);
}
