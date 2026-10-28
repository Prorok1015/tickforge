#include <tickforge/execution/order_manager.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tickforge::execution {

OrderManager::OrderManager(accounting::Account& account, Execution& execution, PaperConfig config, RiskGuard* risk)
    : account_(account), execution_(execution), config_(config), risk_(risk) { validate_config(config); }

std::int64_t OrderManager::limit_buy_reservation(const Order& order) const
{
    // Worst case for the whole quantity: every lot at the limit price with the higher
    // of the two fees. Per-fill rounding can add at most one atom of notional and one of
    // fee per lot, so that slack is reserved too. The unused part is released at the end.
    const auto notional = amount_multiply_divide(order.limit_price_atoms, order.quantity_atoms, 100, true);
    const auto fee = amount_multiply_divide(notional, std::max(config_.fee_ppm, config_.maker_fee_ppm), 1000000, true);
    return amount_add(amount_add(notional, fee), amount_multiply(order.quantity_atoms / config_.quantity_step_atoms, 2));
}

std::optional<OrderReport> OrderManager::submit(const Order& request)
{
    auto order = request;
    const auto reject = [&](std::string reason) {
        OrderReport report{order, false, {order.id, OrderStatus::rejected, {}, order.quantity_atoms, std::move(reason)},
            account_.balances(), account_.balances(), account_.position()};
        report.final = true;
        return report;
    };
    if (order.id <= 0 || !submitted_.insert(order.id).second) return reject("invalid or duplicate order ID");
    // The guard does not trust the strategy: every request is counted and checked.
    if (risk_)
        if (auto reason = risk_->admit(order)) return reject(std::move(*reason));
    const bool limit = order.type == OrderType::limit;
    if (order.quantity_atoms <= 0 || order.quantity_atoms % config_.quantity_step_atoms != 0 ||
        (order.side != Side::buy && order.side != Side::sell) ||
        (order.type != OrderType::market && !limit) ||
        (limit ? order.limit_price_atoms <= 0 || order.max_spend_quote_atoms != 0
               : order.limit_price_atoms != 0 || order.post_only ||
                 (order.side == Side::buy) != (order.max_spend_quote_atoms > 0) || order.max_spend_quote_atoms < 0))
        return reject("invalid order");
    if (order.quantity_atoms > config_.max_order_quantity_atoms) return reject("maximum order quantity exceeded");
    if (!execution_.available()) return reject("book unavailable or stale");
    try {
        if (order.side == Side::buy && amount_add(amount_add(account_.balances().base_total,
            account_.pending_buy_quantity()), order.quantity_atoms) > config_.max_position_quantity_atoms)
            return reject("maximum BTC position exceeded");
        // A limit buy's budget is derived from its price, so the reservation always covers it.
        if (limit && order.side == Side::buy) order.max_spend_quote_atoms = limit_buy_reservation(order);
        account_.reserve(order);
    } catch (const std::runtime_error& error) { return reject(error.what()); }
    pending_.emplace(order.id, Pending{order, account_.balances()});
    // If submit throws, the result is uncertain: keep the reservation and stop.
    execution_.submit(order);
    return std::nullopt;
}

bool OrderManager::cancel(std::int64_t order_id)
{
    const auto found = pending_.find(order_id);
    if (found == pending_.end() || found->second.cancel_requested) return false;
    // Cancels are never refused by risk: they only reduce exposure. The order may still
    // fill before the cancel arrives; its reports decide what actually happened.
    found->second.cancel_requested = true;
    execution_.cancel(order_id);
    return true;
}

std::size_t OrderManager::cancel_open(std::optional<Side> side)
{
    std::size_t sent = 0;
    for (const auto& [id, pending] : pending_)
        if (pending.order.type == OrderType::limit && (!side || pending.order.side == *side) && !pending.cancel_requested)
            sent += cancel(id) ? 1 : 0;
    return sent;
}

std::vector<OpenOrder> OrderManager::open_orders() const
{
    std::vector<OpenOrder> result;
    for (const auto& [id, pending] : pending_)
        result.push_back({pending.order, pending.filled, pending.cancel_requested});
    return result;
}

std::vector<OrderReport> OrderManager::collect()
{
    std::vector<OrderReport> reports;
    for (auto& report : execution_.take_reports()) reports.push_back(commit(std::move(report)));
    return reports;
}

OrderReport OrderManager::commit(ExecutionReport report)
{
    auto& result = report.result;
    const auto found = pending_.find(result.order_id);
    if (found == pending_.end()) throw std::runtime_error("execution reported an unknown order; reservations retained");
    auto& pending = found->second;
    const auto order = pending.order;
    if (result.remaining_quantity_atoms < 0 || result.remaining_quantity_atoms > order.quantity_atoms)
        throw std::runtime_error("invalid execution result; reservation retained");
    auto candidate = account_;
    auto filled = pending.filled;
    auto fees = pending.fees;
    std::vector<Fill> confirmed;
    for (const auto& fill : result.fills) {
        if (fill.order_id != order.id || fill.side != order.side)
            throw std::runtime_error("execution fill belongs to another order; reservation retained");
        if (candidate.apply_fill(fill)) {
            filled = amount_add(filled, fill.quantity_atoms);
            fees = amount_add(fees, fill.fee_quote_atoms);
            confirmed.push_back(fill);
        }
    }
    // Reports carry only new fills; consistency is checked against the running total.
    const auto remaining = amount_subtract(order.quantity_atoms, filled);
    const bool resting = result.status == OrderStatus::open;
    if (remaining != result.remaining_quantity_atoms ||
        (resting && (order.type != OrderType::limit || remaining == 0)) ||
        (result.status == OrderStatus::filled && remaining != 0) ||
        (result.status == OrderStatus::partially_filled && (filled == 0 || remaining == 0)) ||
        ((result.status == OrderStatus::cancelled || result.status == OrderStatus::rejected) && filled != 0))
        throw std::runtime_error("inconsistent execution report; reservation retained");
    if (!resting) candidate.release(order.id);
    account_ = std::move(candidate);
    result.fills = std::move(confirmed);
    OrderReport out{order, true, std::move(result), pending.reserved, account_.balances(), account_.position(),
                    report.executed_ns, report.reported_ns, report.interruption};
    out.final = !resting;
    out.filled_quantity_atoms = filled;
    out.fee_quote_atoms = fees;
    out.diagnostics = std::move(report.diagnostics);
    if (resting) {
        pending.filled = filled;
        pending.fees = fees;
    } else {
        pending_.erase(found);
    }
    return out;
}
} // namespace tickforge::execution
