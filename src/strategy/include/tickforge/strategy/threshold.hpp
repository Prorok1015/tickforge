#pragma once
#include <tickforge/strategy/strategy.hpp>

namespace tickforge::strategy {
struct ThresholdConfig {
    std::int64_t buy_price_atoms;
    std::int64_t sell_price_atoms;
    std::int64_t quantity_atoms;
    std::int64_t buy_budget_quote_atoms;
    std::int64_t quantity_step_atoms; // copied from the execution settings
    std::int64_t fee_ppm;             // copied from the execution settings
};
void validate_config(const ThresholdConfig& config);

// Flat: buy at/below the buy threshold. Holding BTC: sell at/above the sell threshold,
// but only a quantity the best bid can absorb and only if the proceeds after the sale
// fee cover the sold share of the cost basis (no realized loss).
// Execution feedback is the next view's confirmed balances, never assumed fills.
class Threshold final : public Strategy {
public:
    explicit Threshold(ThresholdConfig config);
    std::optional<MarketOrder> on_market(const View& view) override;
private:
    ThresholdConfig config_;
    std::int64_t next_id_ = 1;
    std::int64_t last_ordinal_ = 0;
};
} // namespace tickforge::strategy
