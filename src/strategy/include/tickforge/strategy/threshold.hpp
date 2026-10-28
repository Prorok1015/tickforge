#pragma once
#include <tickforge/strategy/rules.hpp>
#include <tickforge/strategy/strategy.hpp>

namespace tickforge::strategy {
struct ThresholdConfig {
    std::int64_t buy_price_atoms;
    std::int64_t sell_price_atoms;
    std::int64_t quantity_atoms;
    std::int64_t buy_budget_quote_atoms;
    std::int64_t quantity_step_atoms; // copied from the execution settings
    std::int64_t fee_ppm;             // copied from the execution settings (taker)
    // Exit when held BTC valued at the best bid is this far below its cost basis
    // (parts per million, before the exit fee). Zero disables the stop.
    std::int64_t stop_loss_ppm = 0;
    // After a stop-loss closes the position, no new buys for this long, measured on
    // the recorded monotonic receive offset. Zero disables; requires a stop-loss.
    std::int64_t cooldown_ns = 0;
    std::int64_t maker_fee_ppm = 0;   // copied from the execution settings; used by makers
};
void validate_config(const ThresholdConfig& config);

// Market orders only. Flat: buy at/below the buy threshold. Holding BTC:
// - stop-loss: if the position lost stop_loss_ppm of its cost, sell it at market,
//   accepting the loss and any slippage;
// - otherwise sell at/above the sell threshold, but only a quantity the best bid can
//   absorb and only if the proceeds after the sale fee cover the sold share of the
//   cost basis (no realized loss).
// Cooldown: once a view confirms that a stop-loss closed the position, buys wait for
// cooldown_ns. Sells are never delayed.
// Execution feedback is the next view's confirmed balances, never assumed fills.
class Threshold final : public Strategy {
public:
    explicit Threshold(ThresholdConfig config);
    std::vector<Action> on_market(const View& view) override;
    Signal last_signal() const { return last_signal_; } // reason for the latest proposal
    // Set only by the call that started a cooldown: end of the buy pause.
    std::optional<std::int64_t> started_cooldown_until() const { return started_cooldown_; }
private:
    ThresholdConfig config_;
    StopCooldown cooldown_;
    std::int64_t next_id_ = 1;
    std::int64_t last_ordinal_ = 0;
    Signal last_signal_ = Signal::none;
    std::optional<std::int64_t> started_cooldown_;
};
} // namespace tickforge::strategy
