#include <tickforge/strategy/threshold_maker.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace tickforge::strategy {
namespace {
const OpenOrder* find_open(const View& view, Side side)
{
    for (const auto& open : view.open_orders)
        if (open.order.side == side) return &open;
    return nullptr;
}
} // namespace

ThresholdMaker::ThresholdMaker(ThresholdConfig config) : config_(config), cooldown_(config.cooldown_ns)
{
    validate_config(config);
}

std::vector<Action> ThresholdMaker::on_market(const View& view)
{
    if (view.time.ordinal <= last_ordinal_) throw std::runtime_error("strategy requires increasing event ordinals");
    last_ordinal_ = view.time.ordinal;
    std::vector<Action> actions;
    if (const auto until = cooldown_.observe(view))
        actions.push_back(Notice{std::format("stop-loss closed the position; no buys until elapsed={} ns", *until)});
    if (!quotes_usable(view)) return actions;
    const auto place = [&](Order order, Signal signal) {
        order.id = next_id_;
        next_id_ = amount_add(next_id_, 1);
        cooldown_.sent(signal);
        actions.push_back(Place{order, signal});
    };
    const auto cancel = [&](const OpenOrder* open) {
        if (open && !open->cancel_requested) actions.push_back(Cancel{open->order.id});
    };
    const auto& balances = view.balances;
    const auto* buy = find_open(view, Side::buy);
    const auto* sell = find_open(view, Side::sell);
    const auto lot = [&](std::int64_t quantity) { return quantity - quantity % config_.quantity_step_atoms; };

    if (balances.base_total > 0) {
        cancel(buy); // A partial entry is not topped up.
        if (view.sells_allowed && stop_triggered(view.bid->price_atoms, balances, view.position, config_.stop_loss_ppm)) {
            // The resting ask reserves our BTC: withdraw it before selling at market.
            if (sell) {
                cancel(sell);
                return actions;
            }
            const auto quantity = lot(std::min(config_.quantity_atoms, balances.available_base()));
            if (quantity > 0 && !buy) place({0, Side::sell, quantity, 0}, Signal::stop_loss);
            return actions;
        }
        if (sell || buy || !view.sells_allowed) return actions;
        const auto quantity = lot(std::min(config_.quantity_atoms, balances.available_base()));
        if (quantity == 0) return actions;
        // Never below the threshold, never at a loss after the maker fee, never crossing.
        const auto price = std::max({config_.sell_price_atoms,
            break_even_price(quantity, balances, view.position, config_.maker_fee_ppm),
            amount_add(view.bid->price_atoms, 1)});
        place({0, Side::sell, quantity, 0, OrderType::limit, price, true}, Signal::take_profit);
        return actions;
    }

    if (buy || sell || !view.buys_allowed || cooldown_.blocks_buys(view.time.receive_elapsed_ns)) return actions;
    // At or below the best bid the order never crosses; join the bid when it is lower.
    const auto price = std::min(config_.buy_price_atoms, view.bid->price_atoms);
    const auto notional = amount_multiply_divide(price, config_.quantity_atoms, 100, true);
    const auto cost = amount_add(notional, amount_multiply_divide(notional, config_.maker_fee_ppm, 1000000, true));
    if (cost <= std::min(config_.buy_budget_quote_atoms, balances.available_quote()))
        place({0, Side::buy, config_.quantity_atoms, 0, OrderType::limit, price, true}, Signal::buy);
    return actions;
}

} // namespace tickforge::strategy
