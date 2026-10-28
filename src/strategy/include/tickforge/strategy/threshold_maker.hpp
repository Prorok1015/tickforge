#pragma once
#include <tickforge/strategy/rules.hpp>
#include <tickforge/strategy/threshold.hpp>

namespace tickforge::strategy {

// The threshold idea with resting post-only limit orders for entries and exits, so
// fills pay the maker fee and no spread, and a market order only for the stop-loss.
// - Flat, nothing resting: bid at min(buy threshold, best bid) for the lot, if the
//   estimated cost fits the budget. The bid never crosses, so post-only never refuses.
// - Holding BTC: cancel a still-resting buy (no topping up a partial fill); once it is
//   gone, ask for the available BTC at max(sell threshold, break-even after the maker
//   fee, best bid + 1 tick). Prices are 0.01 USDT atoms, the BTCUSDT tick.
// - Stop-loss: cancel the resting ask first, because its BTC is reserved; once no sell
//   rests, sell at market. The cooldown after a stop works as in Threshold.
// Orders are not chased: a resting order stays until it fills, the position changes,
// or the risk guard cancels it. The account must belong to this strategy alone.
class ThresholdMaker final : public Strategy {
public:
    explicit ThresholdMaker(ThresholdConfig config);
    std::vector<Action> on_market(const View& view) override;
private:
    ThresholdConfig config_;
    StopCooldown cooldown_;
    std::int64_t next_id_ = 1;
    std::int64_t last_ordinal_ = 0;
};

} // namespace tickforge::strategy
