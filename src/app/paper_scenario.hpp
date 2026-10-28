#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/execution/risk.hpp>
#include <tickforge/strategy/threshold.hpp>
#include <tickforge/strategy/mean_reversion.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tickforge::app {
struct ScriptOrder { std::int64_t after_ordinal; Order order; };
struct ScriptCancel { std::int64_t after_ordinal; std::int64_t order_id; };
// One paper experiment, read from a TOML config (see docs/paper-trading.md) and
// converted to integer atoms. Either scripted orders or a strategy, never both.
struct PaperScenario {
    std::int64_t initial_quote_atoms;
    std::int64_t initial_base_atoms;
    std::int64_t initial_base_cost_atoms;
    execution::PaperConfig config;
    execution::RiskLimits risk;         // [risk]; all limits off when absent
    std::vector<ScriptOrder> orders;
    std::vector<ScriptCancel> cancels;  // sent after the orders of the same event
    std::optional<strategy::ThresholdConfig> strategy;             // threshold and threshold_maker
    std::optional<strategy::MeanReversionConfig> mean_reversion;   // mean_reversion
    std::string strategy_type;          // "threshold", "threshold_maker", or "mean_reversion"
    bool has_strategy() const { return strategy || mean_reversion; }
    std::vector<std::string> overrides; // "key=value" as given, applied before validation
};
// Overrides replace or add scalar values by dotted key, e.g. "strategy.stop_loss=0.3%".
PaperScenario load_paper_scenario(std::string_view path, const std::vector<std::string>& overrides);
}
