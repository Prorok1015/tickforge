#pragma once
#include <tickforge/execution/execution.hpp>
#include <string_view>
#include <vector>

namespace tickforge::app {
struct ScriptOrder { std::int64_t after_ordinal; MarketOrder order; };
struct PaperScenario {
    std::int64_t initial_quote_atoms;
    std::int64_t initial_base_atoms;
    execution::PaperConfig config;
    std::vector<ScriptOrder> orders;
};
PaperScenario load_paper_scenario(std::string_view path);
}
