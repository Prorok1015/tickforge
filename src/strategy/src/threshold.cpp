#include <tickforge/strategy/threshold.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <stdexcept>

namespace tickforge::strategy {
void validate_config(const ThresholdConfig& c)
{
    if (c.buy_price_atoms <= 0 || c.sell_price_atoms <= c.buy_price_atoms ||
        c.quantity_atoms <= 0 || c.buy_budget_quote_atoms <= 0 || c.quantity_step_atoms <= 0 ||
        c.quantity_atoms % c.quantity_step_atoms != 0)
        throw std::runtime_error("invalid threshold strategy prices, quantity, budget, or step");
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
        const auto quantity = std::min(config_.quantity_atoms, view.balances.available_base());
        const auto rounded = quantity - quantity % config_.quantity_step_atoms;
        if (rounded > 0) order = MarketOrder{next_id_, Side::sell, rounded, 0};
    }
    if (order) next_id_ = amount_add(next_id_, 1);
    return order;
}
} // namespace tickforge::strategy
