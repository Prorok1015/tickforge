#include <tickforge/strategy/threshold.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <stdexcept>

namespace tickforge::strategy {
namespace {
// Mirrors the simulator and Account rounding: proceeds down, fee up, sold cost up.
// Deciding with different arithmetic than the ledger would misjudge break-even sales.
bool covers_cost(const View& view, std::int64_t quantity, std::int64_t fee_ppm)
{
    const auto proceeds = amount_multiply_divide(view.bid->price_atoms, quantity, 100, false);
    const auto fee = amount_multiply_divide(proceeds, fee_ppm, 1000000, true);
    const auto held = view.balances.base_total;
    const auto cost = view.position.base_cost_quote;
    const auto sold_cost = quantity == held ? cost : amount_fraction(cost, quantity, held, true);
    return amount_subtract(proceeds, fee) >= sold_cost;
}
} // namespace

void validate_config(const ThresholdConfig& c)
{
    if (c.buy_price_atoms <= 0 || c.sell_price_atoms <= c.buy_price_atoms ||
        c.quantity_atoms <= 0 || c.buy_budget_quote_atoms <= 0 || c.quantity_step_atoms <= 0 ||
        c.quantity_atoms % c.quantity_step_atoms != 0 || c.fee_ppm < 0 || c.fee_ppm > 1000000)
        throw std::runtime_error("invalid threshold strategy prices, quantity, budget, step, or fee");
}
Threshold::Threshold(ThresholdConfig config) : config_(config) { validate_config(config); }

std::optional<MarketOrder> Threshold::on_market(const View& view)
{
    if (view.time.ordinal <= last_ordinal_) throw std::runtime_error("strategy requires increasing event ordinals");
    last_ordinal_ = view.time.ordinal;
    if (!view.available || !view.bid || !view.ask || view.bid->price_atoms <= 0 ||
        view.ask->price_atoms <= 0 || view.bid->price_atoms >= view.ask->price_atoms ||
        view.bid->quantity_atoms <= 0 || view.ask->quantity_atoms <= 0 ||
        view.balances.base_reserved != 0 || view.balances.quote_reserved != 0) return std::nullopt;
    std::optional<MarketOrder> order;
    if (view.balances.base_total == 0 && view.ask->price_atoms <= config_.buy_price_atoms) {
        const auto budget = std::min(config_.buy_budget_quote_atoms, view.balances.available_quote());
        if (budget > 0) order = MarketOrder{next_id_, Side::buy, config_.quantity_atoms, budget};
    } else if (view.balances.base_total > 0 && view.bid->price_atoms >= config_.sell_price_atoms) {
        // Never sell more than the best bid holds: the whole sale then fills at the checked price.
        const auto quantity = std::min({config_.quantity_atoms, view.balances.available_base(), view.bid->quantity_atoms});
        const auto rounded = quantity - quantity % config_.quantity_step_atoms;
        if (rounded > 0 && covers_cost(view, rounded, config_.fee_ppm))
            order = MarketOrder{next_id_, Side::sell, rounded, 0};
    }
    if (order) next_id_ = amount_add(next_id_, 1);
    return order;
}
} // namespace tickforge::strategy
