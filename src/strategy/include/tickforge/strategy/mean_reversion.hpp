#pragma once
#include <tickforge/strategy/rules.hpp>
#include <tickforge/strategy/strategy.hpp>
#include <deque>
#include <optional>
#include <string>

namespace tickforge::strategy {
struct MeanReversionConfig {
    std::int64_t window_ns;              // the average covers this much recent time; also the warm-up
    std::int64_t entry_below_ppm;        // buy when the best ask is this far below the average
    std::int64_t take_profit_ppm;        // sell when the bid is this far above the average cost
    std::int64_t quantity_atoms;
    std::int64_t buy_budget_quote_atoms;
    std::int64_t quantity_step_atoms;    // copied from the execution settings
    std::int64_t fee_ppm;                // copied from the execution settings (taker)
    std::int64_t stop_loss_ppm = 0;
    std::int64_t cooldown_ns = 0;
    // A longer stretch without usable quotes clears the history: the warm-up restarts.
    std::int64_t max_gap_ns = 10000000000;
};
void validate_config(const MeanReversionConfig& config);

// Mean reversion with market orders and no absolute prices in its config. Once per
// second of session time it samples the mid price; the average of the last `window`
// of samples is its reference. Until the window is full it only watches (warm-up).
// Flat: buy when the best ask is entry_below below the average. Holding BTC: the same
// exits as Threshold, relative to the purchase instead of a fixed price: stop-loss
// at stop_loss below cost, otherwise sell once the bid is take_profit above the
// average cost and the sale covers the cost after its fee. Exits do not need the
// average, so they continue through a warm-up. Seconds without any event repeat the
// last mid: no event means an unchanged book. All time is event time.
class MeanReversion final : public Strategy {
public:
    static constexpr std::int64_t sample_ns = 1000000000;
    explicit MeanReversion(MeanReversionConfig config);
    std::vector<Action> on_market(const View& view) override;
    std::string status() const override;
    bool warm() const { return samples_.size() == window_samples_; }
    // Average mid price in price atoms (half ticks rounded down), once warm.
    std::optional<std::int64_t> average_price() const;
    // The highest ask that triggers a buy, once warm.
    std::optional<std::int64_t> entry_price() const;
private:
    void sample(std::int64_t now_ns, std::int64_t mid2);
    MeanReversionConfig config_;
    StopCooldown cooldown_;
    std::size_t window_samples_;
    std::deque<std::int64_t> samples_; // bid + ask (twice the mid) per second
    std::int64_t sum_ = 0;
    std::optional<std::int64_t> last_slot_, last_usable_ns_, last_mid2_;
    std::int64_t now_ns_ = 0;
    std::int64_t next_id_ = 1;
    std::int64_t last_ordinal_ = 0;
    bool announced_warm_ = false;
};
} // namespace tickforge::strategy
