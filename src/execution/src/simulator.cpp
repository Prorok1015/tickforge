#include <tickforge/execution/simulator.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tickforge::execution {
namespace {
std::int64_t notional(const MarketOrder& order, std::int64_t price, std::int64_t quantity)
{
    return amount_multiply_divide(price, quantity, 100, order.side == Side::buy);
}
std::int64_t fee(std::int64_t quote, std::int64_t ppm)
{
    return amount_multiply_divide(quote, ppm, 1000000, true);
}
}

void validate_config(const PaperConfig& config)
{
    if (config.fee_ppm < 0 || config.fee_ppm > 1000000 || config.quantity_step_atoms <= 0 ||
        config.max_order_quantity_atoms <= 0 || config.max_position_quantity_atoms <= 0 || config.stale_after_ns < 0 ||
        config.order_latency_ns < 0 || config.report_latency_ns < 0)
        throw std::invalid_argument("invalid paper trading configuration");
}

Simulator::Simulator(PaperConfig config) : config_(config) { validate_config(config); }

void Simulator::on_market_event(const MarketEvent& event)
{
    if (ordinal_ == std::numeric_limits<std::int64_t>::max() || event.time.ordinal != ordinal_ + 1 ||
        event.time.receive_elapsed_ns < now_ns_)
        throw std::runtime_error("invalid simulator event order/time");
    if (finished_) throw std::runtime_error("simulator already finished");
    // The book still shows every event up to the due time: execute before applying this one.
    run_due(event.time.receive_elapsed_ns);
    ordinal_ = event.time.ordinal;
    now_ns_ = event.time.receive_elapsed_ns;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (!std::is_same_v<T, Trade>) {
            const auto result = tracker_.apply(data);
            if constexpr (std::is_same_v<T, BookSnapshot> || std::is_same_v<T, BookReset>) {
                consumed_bids_.clear(); consumed_asks_.clear();
            }
            if (!tracker_.view().synchronized()) {
                consumed_bids_.clear(); consumed_asks_.clear();
                last_fresh_ns_.reset();
            } else if (result == BookResult::applied || result == BookResult::snapshot) {
                last_fresh_ns_ = now_ns_;
                if constexpr (std::is_same_v<T, BookUpdate>) {
                    // Only explicit absolute-size updates replenish virtual liquidity.
                    for (const auto& level : data.bids) consumed_bids_.erase(level.price_atoms);
                    for (const auto& level : data.asks) consumed_asks_.erase(level.price_atoms);
                }
            }
        }
    }, event.data);
}

bool Simulator::available() const { return available_at(now_ns_); }

bool Simulator::available_at(std::int64_t time) const
{
    return tracker_.view().synchronized() && last_fresh_ns_ && time - *last_fresh_ns_ <= config_.stale_after_ns;
}

void Simulator::submit(const MarketOrder& order)
{
    if (finished_) throw std::runtime_error("simulator already finished");
    const auto due = amount_add(now_ns_, config_.order_latency_ns);
    if (config_.order_latency_ns == 0) {
        reports_.push_back({execute(order), now_ns_, amount_add(now_ns_, config_.report_latency_ns)});
        return;
    }
    in_flight_.push_back({order, due});
}

void Simulator::run_due(std::int64_t before_ns)
{
    while (!in_flight_.empty() && in_flight_.front().due_ns < before_ns) {
        const auto [order, due] = in_flight_.front();
        in_flight_.pop_front();
        if (!available_at(due)) cancel(order, due, Interruption::no_trusted_book);
        else reports_.push_back({execute_at(order, due), due, amount_add(due, config_.report_latency_ns)});
    }
}

void Simulator::cancel(const MarketOrder& order, std::int64_t time, Interruption why)
{
    const auto reason = why == Interruption::no_trusted_book ? "no trusted book at execution time; order cancelled"
                                                              : "market data ended before execution; order cancelled";
    reports_.push_back({{order.id, OrderStatus::cancelled, {}, order.quantity_atoms, reason}, time,
                        amount_add(time, config_.report_latency_ns), why});
}

std::vector<ExecutionReport> Simulator::take_reports()
{
    // Reports are produced in execution order with a fixed delay, so they are sorted by report time.
    const auto ready = finished_ ? reports_.end() : std::find_if(reports_.begin(), reports_.end(),
        [&](const ExecutionReport& report) { return report.reported_ns > now_ns_; });
    std::vector<ExecutionReport> result(std::make_move_iterator(reports_.begin()), std::make_move_iterator(ready));
    reports_.erase(reports_.begin(), ready);
    return result;
}

void Simulator::finish()
{
    for (const auto& pending : in_flight_) cancel(pending.order, now_ns_, Interruption::session_ended);
    in_flight_.clear();
    finished_ = true;
}

std::optional<Level> Simulator::executable_level(Side side) const
{
    const auto& consumed = side == Side::buy ? consumed_asks_ : consumed_bids_;
    // Common case: nothing consumed on this side, so the book's best level is exact.
    if (consumed.empty()) return side == Side::buy ? book().best_ask() : book().best_bid();
    for (const auto& level : side == Side::buy ? book().ask_depth() : book().bid_depth()) {
        const auto found = consumed.find(level.price_atoms);
        const auto used = found == consumed.end() ? 0 : found->second;
        if (level.quantity_atoms > used) return Level{level.price_atoms, level.quantity_atoms - used};
    }
    return std::nullopt;
}

ExecutionResult Simulator::execute(const MarketOrder& order) { return execute_at(order, now_ns_); }

ExecutionResult Simulator::execute_at(const MarketOrder& order, std::int64_t time)
{
    ExecutionResult result{order.id, OrderStatus::rejected, {}, order.quantity_atoms, ""};
    if (order.id <= 0 || order.quantity_atoms <= 0 || order.quantity_atoms % config_.quantity_step_atoms != 0 ||
        (order.side != Side::buy && order.side != Side::sell) ||
        (order.side == Side::buy && order.max_spend_quote_atoms <= 0) ||
        (order.side == Side::sell && order.max_spend_quote_atoms != 0)) {
        result.reason = "invalid market order";
        return result;
    }
    if (!available_at(time)) { result.reason = "book unavailable or stale"; return result; }
    const auto levels = order.side == Side::buy ? book().ask_depth() : book().bid_depth();
    auto consumed = order.side == Side::buy ? consumed_asks_ : consumed_bids_;
    auto next_id = next_fill_id_;
    auto budget = order.max_spend_quote_atoms;
    bool budget_limited = false;
    for (const auto& level : levels) {
        if (result.remaining_quantity_atoms == 0) break;
        const auto found = consumed.find(level.price_atoms);
        const auto used = found == consumed.end() ? 0 : found->second;
        const auto available_quantity = level.quantity_atoms > used ? level.quantity_atoms - used : 0;
        auto lots = std::min(available_quantity, result.remaining_quantity_atoms) / config_.quantity_step_atoms;
        if (order.side == Side::buy) {
            // Find the largest affordable number of lots, including rounded fees.
            std::int64_t low = 0, high = lots;
            while (low < high) {
                const auto middle = low + (high - low) / 2 + 1;
                bool affordable = false;
                try {
                    const auto quote = notional(order, level.price_atoms, amount_multiply(middle, config_.quantity_step_atoms));
                    affordable = amount_add(quote, fee(quote, config_.fee_ppm)) <= budget;
                } catch (const std::overflow_error&) { /* An overflowing cost cannot fit the finite budget. */ }
                if (affordable) low = middle;
                else high = middle - 1;
            }
            if (low < lots) budget_limited = true;
            lots = low;
        }
        if (lots == 0) continue;
        const auto quantity = amount_multiply(lots, config_.quantity_step_atoms);
        const auto quote = notional(order, level.price_atoms, quantity);
        const auto commission = fee(quote, config_.fee_ppm);
        const auto fill_id = next_id;
        next_id = amount_add(next_id, 1);
        result.fills.push_back({fill_id, order.id, order.side, level.price_atoms, quantity, quote, commission});
        result.remaining_quantity_atoms -= quantity;
        if (order.side == Side::buy) budget = amount_subtract(budget, amount_add(quote, commission));
        consumed[level.price_atoms] = amount_add(used, quantity);
    }
    if (result.remaining_quantity_atoms == 0) result.status = OrderStatus::filled;
    else if (!result.fills.empty()) result.status = OrderStatus::partially_filled;
    else result.status = OrderStatus::cancelled;
    if (result.remaining_quantity_atoms == 0) result.reason = "filled";
    else if (budget_limited) result.reason = "buy budget including fees exhausted; remainder cancelled";
    else result.reason = "known virtual liquidity exhausted; remainder cancelled";
    if (order.side == Side::buy) consumed_asks_ = std::move(consumed);
    else consumed_bids_ = std::move(consumed);
    next_fill_id_ = next_id;
    return result;
}
} // namespace tickforge::execution
