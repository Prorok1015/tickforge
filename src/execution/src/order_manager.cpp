#include <tickforge/execution/order_manager.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>
#include <utility>

namespace tickforge::execution {

OrderManager::OrderManager(accounting::Account& account, Execution& execution, PaperConfig config)
    : account_(account), execution_(execution), config_(config) { validate_config(config); }

OrderReport OrderManager::submit(const MarketOrder& order)
{
    const auto reject = [&](std::string reason) {
        return OrderReport{order, false, {order.id, OrderStatus::rejected, {}, order.quantity_atoms, std::move(reason)},
            account_.balances(), account_.balances(), account_.position()};
    };
    if (order.id <= 0 || !submitted_.insert(order.id).second) return reject("invalid or duplicate order ID");
    if (order.quantity_atoms <= 0 || order.quantity_atoms % config_.quantity_step_atoms != 0 ||
        (order.side != Side::buy && order.side != Side::sell) ||
        (order.side == Side::buy && order.max_spend_quote_atoms <= 0) ||
        (order.side == Side::sell && order.max_spend_quote_atoms != 0)) return reject("invalid market order");
    if (order.quantity_atoms > config_.max_order_quantity_atoms) return reject("maximum order quantity exceeded");
    if (!execution_.available()) return reject("book unavailable or stale");
    try {
        if (order.side == Side::buy && amount_add(amount_add(account_.balances().base_total,
            account_.pending_buy_quantity()), order.quantity_atoms) > config_.max_position_quantity_atoms)
            return reject("maximum BTC position exceeded");
        account_.reserve(order);
    } catch (const std::runtime_error& error) { return reject(error.what()); }
    const auto reserved = account_.balances();
    // If execute throws, the result is uncertain: keep the reservation and stop.
    auto result = execution_.execute(order);
    if (result.order_id != order.id || result.remaining_quantity_atoms < 0 ||
        result.remaining_quantity_atoms > order.quantity_atoms)
        throw std::runtime_error("invalid execution result; reservation retained");
    auto candidate = account_;
    std::int64_t filled = 0;
    std::vector<Fill> confirmed;
    for (const auto& fill : result.fills) {
        if (fill.order_id != order.id || fill.side != order.side)
            throw std::runtime_error("execution fill belongs to another order; reservation retained");
        if (candidate.apply_fill(fill)) {
            filled = amount_add(filled, fill.quantity_atoms);
            confirmed.push_back(fill);
        }
    }
    const auto remaining = amount_subtract(order.quantity_atoms, filled);
    if (remaining != result.remaining_quantity_atoms ||
        (result.status == OrderStatus::filled && remaining != 0) ||
        (result.status == OrderStatus::partially_filled && (filled == 0 || remaining == 0)) ||
        ((result.status == OrderStatus::cancelled || result.status == OrderStatus::rejected) && filled != 0) ||
        (result.status != OrderStatus::filled && result.status != OrderStatus::partially_filled &&
         result.status != OrderStatus::cancelled && result.status != OrderStatus::rejected))
        throw std::runtime_error("inconsistent terminal execution result; reservation retained");
    candidate.release(order.id);
    account_ = std::move(candidate);
    result.fills = std::move(confirmed);
    return {order, true, std::move(result), reserved, account_.balances(), account_.position()};
}
} // namespace tickforge::execution
