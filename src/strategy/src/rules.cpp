#include <tickforge/strategy/rules.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>

namespace tickforge::strategy {

bool sale_covers_cost(std::int64_t price_atoms, std::int64_t quantity_atoms, const accounting::Balances& balances,
                      const accounting::Position& position, std::int64_t fee_ppm)
{
    const auto held = balances.base_total;
    if (quantity_atoms <= 0 || quantity_atoms > held) throw std::invalid_argument("sale quantity must be held");
    const auto proceeds = amount_multiply_divide(price_atoms, quantity_atoms, 100, false);
    const auto fee = amount_multiply_divide(proceeds, fee_ppm, 1000000, true);
    const auto cost = position.base_cost_quote;
    const auto sold_cost = quantity_atoms == held ? cost : amount_fraction(cost, quantity_atoms, held, true);
    return amount_subtract(proceeds, fee) >= sold_cost;
}

std::int64_t break_even_price(std::int64_t quantity_atoms, const accounting::Balances& balances,
                              const accounting::Position& position, std::int64_t fee_ppm)
{
    // Covering is monotonic in price, so the smallest covering price can be searched.
    std::int64_t low = 1, high = 1;
    while (!sale_covers_cost(high, quantity_atoms, balances, position, fee_ppm)) {
        low = high + 1;
        high = amount_multiply(high, 2);
    }
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (sale_covers_cost(middle, quantity_atoms, balances, position, fee_ppm)) high = middle;
        else low = middle + 1;
    }
    return low;
}

bool stop_triggered(std::int64_t bid_price_atoms, const accounting::Balances& balances,
                    const accounting::Position& position, std::int64_t stop_loss_ppm)
{
    if (stop_loss_ppm == 0 || balances.base_total == 0) return false;
    const auto value = amount_multiply_divide(bid_price_atoms, balances.base_total, 100, false);
    const auto limit = amount_multiply_divide(position.base_cost_quote, 1000000 - stop_loss_ppm, 1000000, true);
    return value <= limit;
}

std::optional<std::int64_t> StopCooldown::observe(const View& view)
{
    // Balances are confirmed even while quotes are unavailable, so this runs first.
    if (!stop_pending_ || view.balances.base_total != 0 || view.balances.base_reserved != 0) return std::nullopt;
    stop_pending_ = false;
    if (cooldown_ns_ == 0) return std::nullopt;
    until_ = amount_add(view.time.receive_elapsed_ns, cooldown_ns_);
    return until_;
}

bool quotes_usable(const View& view)
{
    return view.available && view.bid && view.ask && view.bid->price_atoms > 0 && view.ask->price_atoms > 0 &&
           view.bid->price_atoms < view.ask->price_atoms && view.bid->quantity_atoms > 0 && view.ask->quantity_atoms > 0;
}

} // namespace tickforge::strategy
