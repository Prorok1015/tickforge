#pragma once
#include <tickforge/strategy/strategy.hpp>
#include <optional>

// Decision rules shared by strategies. They use the same integer rounding as the
// simulator and Account (proceeds down, fees up, sold cost up): deciding with other
// arithmetic than the ledger would misjudge break-even cases.
namespace tickforge::strategy {

// Would selling `quantity` at `price` return at least its share of the cost basis
// after the sale fee? Quantity must not exceed the BTC held.
bool sale_covers_cost(std::int64_t price_atoms, std::int64_t quantity_atoms, const accounting::Balances& balances,
                      const accounting::Position& position, std::int64_t fee_ppm);

// Smallest price at which selling `quantity` covers its cost after the fee.
std::int64_t break_even_price(std::int64_t quantity_atoms, const accounting::Balances& balances,
                              const accounting::Position& position, std::int64_t fee_ppm);

// Held BTC valued at `bid_price` is at least stop_loss_ppm below its cost. The limit
// rounds up, so rounding never delays the stop. Zero disables it.
bool stop_triggered(std::int64_t bid_price_atoms, const accounting::Balances& balances,
                    const accounting::Position& position, std::int64_t stop_loss_ppm);

// The pause after a stop-loss exit. It starts only when a view confirms the position
// is closed: a stop proposal alone may be rejected or fill partially.
class StopCooldown {
public:
    explicit StopCooldown(std::int64_t cooldown_ns) : cooldown_ns_(cooldown_ns) {}
    // Returns the end of a pause that this view started, if any.
    std::optional<std::int64_t> observe(const View& view);
    void sent(Signal signal) { stop_pending_ = signal == Signal::stop_loss; }
    bool blocks_buys(std::int64_t now_ns) const { return until_ && now_ns < *until_; }
private:
    std::int64_t cooldown_ns_;
    bool stop_pending_ = false;
    std::optional<std::int64_t> until_;
};

// A usable two-sided quote: trusted, positive, and not crossed.
bool quotes_usable(const View& view);

} // namespace tickforge::strategy
