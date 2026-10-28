#include <tickforge/execution/simulator.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tickforge::execution {
namespace {
std::int64_t notional(Side side, std::int64_t price, std::int64_t quantity)
{
    return amount_multiply_divide(price, quantity, 100, side == Side::buy);
}
std::int64_t fee(std::int64_t quote, std::int64_t ppm)
{
    return amount_multiply_divide(quote, ppm, 1000000, true);
}
}

void validate_config(const PaperConfig& config)
{
    if (config.fee_ppm < 0 || config.fee_ppm > 1000000 || config.maker_fee_ppm < 0 || config.maker_fee_ppm > 1000000 ||
        config.quantity_step_atoms <= 0 || config.max_order_quantity_atoms <= 0 ||
        config.max_position_quantity_atoms <= 0 || config.stale_after_ns < 0 ||
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
    // The book still shows every event up to the due time: act before applying this one.
    run_due(event.time.receive_elapsed_ns);
    ordinal_ = event.time.ordinal;
    now_ns_ = event.time.receive_elapsed_ns;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Trade>) {
            match_trade(data, now_ns_);
        } else {
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
                match_book(now_ns_);
            }
        }
    }, event.data);
}

bool Simulator::available() const { return available_at(now_ns_); }

bool Simulator::available_at(std::int64_t time) const
{
    return tracker_.view().synchronized() && last_fresh_ns_ && time - *last_fresh_ns_ <= config_.stale_after_ns;
}

void Simulator::submit(const Order& order)
{
    if (finished_) throw std::runtime_error("simulator already finished");
    if (config_.order_latency_ns == 0) arrive(order, now_ns_);
    else in_flight_.push_back({order, amount_add(now_ns_, config_.order_latency_ns)});
}

void Simulator::cancel(std::int64_t order_id)
{
    if (finished_) throw std::runtime_error("simulator already finished");
    if (config_.order_latency_ns == 0) cancel_resting(order_id, now_ns_, Interruption::none);
    else in_flight_.push_back({order_id, amount_add(now_ns_, config_.order_latency_ns)});
}

void Simulator::run_due(std::int64_t before_ns)
{
    while (!in_flight_.empty() && in_flight_.front().due_ns < before_ns) {
        const auto action = in_flight_.front();
        in_flight_.pop_front();
        if (const auto* order = std::get_if<Order>(&action.what)) arrive(*order, action.due_ns);
        else cancel_resting(std::get<std::int64_t>(action.what), action.due_ns, Interruption::none);
    }
}

void Simulator::report(ExecutionResult result, std::int64_t time, Interruption why)
{
    reports_.push_back({std::move(result), time, amount_add(time, config_.report_latency_ns), why});
}

void Simulator::interrupt(const Order& order, std::int64_t time, Interruption why)
{
    const auto reason = why == Interruption::no_trusted_book ? "no trusted book at execution time; order cancelled"
                                                              : "market data ended before execution; order cancelled";
    report({order.id, OrderStatus::cancelled, {}, order.quantity_atoms, reason}, time, why);
}

void Simulator::arrive(const Order& order, std::int64_t time)
{
    if (!available_at(time)) return interrupt(order, time, Interruption::no_trusted_book);
    if (order.type == OrderType::market) return report(take(order, time, std::nullopt), time);
    const auto limit = order.limit_price_atoms;
    if (limit <= 0) return report({order.id, OrderStatus::rejected, {}, order.quantity_atoms, "invalid limit price"}, time);
    const auto opposite = executable_level(order.side);
    const bool crosses = opposite &&
        (order.side == Side::buy ? opposite->price_atoms <= limit : opposite->price_atoms >= limit);
    if (crosses && order.post_only)
        return report({order.id, OrderStatus::rejected, {}, order.quantity_atoms,
                       "post-only limit order would cross the spread"}, time);
    // The crossing part takes liquidity like a market order, but never beyond the limit.
    auto result = crosses ? take(order, time, limit)
                          : ExecutionResult{order.id, OrderStatus::open, {}, order.quantity_atoms, ""};
    if (result.status == OrderStatus::rejected || result.remaining_quantity_atoms == 0)
        return report(std::move(result), time);
    std::int64_t spent = 0;
    for (const auto& fill : result.fills) spent = amount_add(spent, amount_add(fill.quote_atoms, fill.fee_quote_atoms));
    // We join the back of the queue: everything already visible at our price is ahead.
    const auto ahead = level_size(order.side, limit);
    resting_.push_back({order, result.remaining_quantity_atoms, ahead,
                        order.side == Side::buy ? amount_subtract(order.max_spend_quote_atoms, spent) : 0,
                        !result.fills.empty()});
    result.status = OrderStatus::open;
    result.reason = std::format("resting; {} atoms ahead in queue", ahead);
    report(std::move(result), time);
}

std::int64_t Simulator::level_size(Side side, std::int64_t price) const
{
    return side == Side::buy ? book().bid_size(price) : book().ask_size(price);
}

std::optional<std::int64_t> Simulator::queue_ahead(std::int64_t order_id) const
{
    for (const auto& resting : resting_)
        if (resting.order.id == order_id) return resting.queue_ahead;
    return std::nullopt;
}

void Simulator::fill_resting(Resting& resting, std::int64_t quantity, std::vector<Fill>& fills)
{
    const auto lots = std::min(quantity, resting.remaining) / config_.quantity_step_atoms;
    if (lots == 0) return;
    const auto size = amount_multiply(lots, config_.quantity_step_atoms);
    const auto& order = resting.order;
    // A maker fill happens at our resting price, not at the aggressor's price.
    const auto quote = notional(order.side, order.limit_price_atoms, size);
    const auto commission = fee(quote, config_.maker_fee_ppm);
    if (order.side == Side::buy) {
        const auto cost = amount_add(quote, commission);
        if (cost > resting.budget_left) throw std::logic_error("limit buy reservation does not cover a maker fill");
        resting.budget_left -= cost;
    }
    fills.push_back({next_fill_id_, order.id, order.side, order.limit_price_atoms, size, quote, commission, Liquidity::maker});
    next_fill_id_ = amount_add(next_fill_id_, 1);
    resting.remaining -= size;
    resting.filled_any = true;
}

void Simulator::report_fills(Resting& resting, std::vector<Fill> fills, std::int64_t time, std::string_view done)
{
    if (fills.empty()) return;
    const auto filled = resting.remaining == 0;
    report({resting.order.id, filled ? OrderStatus::filled : OrderStatus::open, std::move(fills), resting.remaining,
            filled ? std::string(done) : "partially filled; still resting"}, time);
}

void Simulator::match_trade(const Trade& trade, std::int64_t time)
{
    // Our fills at one price within one trade are shared in arrival order.
    std::map<std::pair<Side, std::int64_t>, std::int64_t> given;
    for (auto& resting : resting_) {
        const auto& order = resting.order;
        const auto price = order.limit_price_atoms;
        const bool buy = order.side == Side::buy;
        std::vector<Fill> fills;
        // A trade at a worse price than ours means the market traded through our level:
        // by price priority we would have been filled first. Valid for any aggressor.
        if (buy ? trade.price_atoms < price : trade.price_atoms > price) {
            fill_resting(resting, resting.remaining, fills);
        } else if (trade.price_atoms == price && trade.aggressor == (buy ? Aggressor::sell : Aggressor::buy)) {
            // The trade first consumes the queue ahead of us, then reaches us.
            const auto consumed = std::min(trade.quantity_atoms, resting.queue_ahead);
            resting.queue_ahead -= consumed;
            auto& shared = given[{order.side, price}];
            const auto reaching = std::max<std::int64_t>(0, trade.quantity_atoms - consumed - shared);
            const auto before = resting.remaining;
            fill_resting(resting, reaching, fills);
            shared += before - resting.remaining;
        }
        report_fills(resting, std::move(fills), time, "filled");
    }
    std::erase_if(resting_, [](const Resting& resting) { return resting.remaining == 0; });
}

void Simulator::match_book(std::int64_t time)
{
    for (auto& resting : resting_) {
        const auto& order = resting.order;
        const auto price = order.limit_price_atoms;
        const bool buy = order.side == Side::buy;
        // A level only shrinks by trades or cancellations; trades were already counted,
        // so shrinking below our queue estimate means those ahead left. Growth is behind us.
        resting.queue_ahead = std::min(resting.queue_ahead, level_size(order.side, price));
        // The other side now offers our price or better: in reality those orders would
        // have traded with our resting order first, at our price.
        const auto opposite = executable_level(order.side);
        if (!opposite || (buy ? opposite->price_atoms > price : opposite->price_atoms < price)) continue;
        auto& consumed = buy ? consumed_asks_ : consumed_bids_;
        std::vector<Fill> fills;
        for (const auto& level : buy ? book().ask_depth() : book().bid_depth()) {
            if (resting.remaining == 0 || (buy ? level.price_atoms > price : level.price_atoms < price)) break;
            auto& used = consumed[level.price_atoms];
            const auto before = resting.remaining;
            fill_resting(resting, level.quantity_atoms > used ? level.quantity_atoms - used : 0, fills);
            used = amount_add(used, before - resting.remaining);
        }
        report_fills(resting, std::move(fills), time, "filled by a crossing book");
    }
    std::erase_if(resting_, [](const Resting& resting) { return resting.remaining == 0; });
}

void Simulator::cancel_resting(std::int64_t order_id, std::int64_t time, Interruption why)
{
    const auto found = std::find_if(resting_.begin(), resting_.end(),
        [&](const Resting& resting) { return resting.order.id == order_id; });
    if (found == resting_.end()) return; // Too late (already filled) or unknown: nothing to cancel.
    report({order_id, found->filled_any ? OrderStatus::partially_filled : OrderStatus::cancelled, {},
            found->remaining, why == Interruption::session_ended ? "market data ended; resting order cancelled"
                                                                : "cancelled by request"}, time, why);
    resting_.erase(found);
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
    for (const auto& action : in_flight_)
        if (const auto* order = std::get_if<Order>(&action.what)) interrupt(*order, now_ns_, Interruption::session_ended);
    in_flight_.clear();
    while (!resting_.empty()) cancel_resting(resting_.front().order.id, now_ns_, Interruption::session_ended);
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

ExecutionResult Simulator::execute(const Order& order) { return take(order, now_ns_, std::nullopt); }

ExecutionResult Simulator::take(const Order& order, std::int64_t time, std::optional<std::int64_t> price_cap)
{
    ExecutionResult result{order.id, OrderStatus::rejected, {}, order.quantity_atoms, ""};
    if (order.id <= 0 || order.quantity_atoms <= 0 || order.quantity_atoms % config_.quantity_step_atoms != 0 ||
        (order.side != Side::buy && order.side != Side::sell) ||
        (order.side == Side::buy && order.max_spend_quote_atoms <= 0) ||
        (order.side == Side::sell && order.max_spend_quote_atoms != 0)) {
        result.reason = "invalid order";
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
        if (price_cap && (order.side == Side::buy ? level.price_atoms > *price_cap : level.price_atoms < *price_cap)) break;
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
                    const auto quote = notional(order.side, level.price_atoms, amount_multiply(middle, config_.quantity_step_atoms));
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
        const auto quote = notional(order.side, level.price_atoms, quantity);
        const auto commission = fee(quote, config_.fee_ppm);
        const auto fill_id = next_id;
        next_id = amount_add(next_id, 1);
        result.fills.push_back({fill_id, order.id, order.side, level.price_atoms, quantity, quote, commission, Liquidity::taker});
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
