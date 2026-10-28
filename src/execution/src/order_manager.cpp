#include <tickforge/execution/order_manager.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>
#include <utility>

namespace tickforge::execution {

OrderManager::OrderManager(accounting::Account& account, Execution& execution, PaperConfig config, RiskGuard* risk)
    : account_(account), execution_(execution), config_(config), risk_(risk) { validate_config(config); }

std::optional<OrderReport> OrderManager::submit(const MarketOrder& order)
{
    const auto reject = [&](std::string reason) {
        return OrderReport{order, false, {order.id, OrderStatus::rejected, {}, order.quantity_atoms, std::move(reason)},
            account_.balances(), account_.balances(), account_.position()};
    };
    if (order.id <= 0 || !submitted_.insert(order.id).second) return reject("invalid or duplicate order ID");
    // The guard does not trust the strategy: every request is counted and checked.
    if (risk_)
        if (auto reason = risk_->admit(order)) return reject(std::move(*reason));
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
    pending_.emplace(order.id, Pending{order, account_.balances()});
    // If submit throws, the result is uncertain: keep the reservation and stop.
    execution_.submit(order);
    return std::nullopt;
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
    const auto order = found->second.order;
    if (result.remaining_quantity_atoms < 0 || result.remaining_quantity_atoms > order.quantity_atoms)
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
    const auto reserved = found->second.reserved;
    pending_.erase(found);
    return {order, true, std::move(result), reserved, account_.balances(), account_.position(),
            report.executed_ns, report.reported_ns, report.interruption};
}
} // namespace tickforge::execution
