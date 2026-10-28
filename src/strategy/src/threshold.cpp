#include <tickforge/strategy/threshold.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace tickforge::strategy {

void validate_config(const ThresholdConfig& c)
{
    if (c.buy_price_atoms <= 0 || c.sell_price_atoms <= c.buy_price_atoms ||
        c.quantity_atoms <= 0 || c.buy_budget_quote_atoms <= 0 || c.quantity_step_atoms <= 0 ||
        c.quantity_atoms % c.quantity_step_atoms != 0 || c.fee_ppm < 0 || c.fee_ppm > 1000000 ||
        c.maker_fee_ppm < 0 || c.maker_fee_ppm > 1000000 ||
        c.stop_loss_ppm < 0 || c.stop_loss_ppm >= 1000000 || c.cooldown_ns < 0 ||
        (c.cooldown_ns > 0 && c.stop_loss_ppm == 0))
        throw std::runtime_error("invalid threshold strategy prices, quantity, budget, step, fee, stop-loss, or cooldown");
}

Threshold::Threshold(ThresholdConfig config) : config_(config), cooldown_(config.cooldown_ns) { validate_config(config); }

std::vector<Action> Threshold::on_market(const View& view)
{
    if (view.time.ordinal <= last_ordinal_) throw std::runtime_error("strategy requires increasing event ordinals");
    last_ordinal_ = view.time.ordinal;
    last_signal_ = Signal::none;
    std::vector<Action> actions;
    started_cooldown_ = cooldown_.observe(view);
    if (started_cooldown_)
        actions.push_back(Notice{std::format("stop-loss closed the position; no buys until elapsed={} ns", *started_cooldown_)});
    if (!quotes_usable(view) || view.balances.base_reserved != 0 || view.balances.quote_reserved != 0) return actions;
    std::optional<Order> order;
    auto signal = Signal::none;
    const auto& balances = view.balances;
    if (balances.base_total == 0 && view.buys_allowed && !cooldown_.blocks_buys(view.time.receive_elapsed_ns) &&
        view.ask->price_atoms <= config_.buy_price_atoms) {
        const auto budget = std::min(config_.buy_budget_quote_atoms, balances.available_quote());
        if (budget > 0) order = Order{next_id_, Side::buy, config_.quantity_atoms, budget};
        signal = Signal::buy;
    } else if (balances.base_total > 0 && view.sells_allowed &&
               stop_triggered(view.bid->price_atoms, balances, view.position, config_.stop_loss_ppm)) {
        // Exit at market: the stop prefers leaving to waiting, so no bid-size cap and no
        // break-even check. The lot still caps the request, as for any order.
        const auto quantity = std::min(config_.quantity_atoms, balances.available_base());
        const auto rounded = quantity - quantity % config_.quantity_step_atoms;
        if (rounded > 0) order = Order{next_id_, Side::sell, rounded, 0};
        signal = Signal::stop_loss;
    } else if (balances.base_total > 0 && view.sells_allowed && view.bid->price_atoms >= config_.sell_price_atoms) {
        // Never sell more than the best bid holds: the whole sale then fills at the checked price.
        const auto quantity = std::min({config_.quantity_atoms, balances.available_base(), view.bid->quantity_atoms});
        const auto rounded = quantity - quantity % config_.quantity_step_atoms;
        if (rounded > 0 && sale_covers_cost(view.bid->price_atoms, rounded, balances, view.position, config_.fee_ppm))
            order = Order{next_id_, Side::sell, rounded, 0};
        signal = Signal::take_profit;
    }
    if (order) {
        next_id_ = amount_add(next_id_, 1);
        last_signal_ = signal;
        cooldown_.sent(signal);
        actions.push_back(Place{*order, signal});
    }
    return actions;
}
} // namespace tickforge::strategy
