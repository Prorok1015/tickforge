#include <tickforge/execution/risk.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>

namespace tickforge::execution {
namespace {
constexpr std::int64_t minute_ns = 60000000000;
std::string trigger_text(RiskTrigger trigger) { return std::string(trigger_name(trigger)); }
} // namespace

std::string_view trigger_name(RiskTrigger trigger)
{
    switch (trigger) {
    case RiskTrigger::max_loss: return "max_loss";
    case RiskTrigger::max_drawdown: return "max_drawdown";
    case RiskTrigger::order_rate: return "order_rate";
    case RiskTrigger::none: break;
    }
    return "none";
}

void validate_limits(const RiskLimits& limits)
{
    if (limits.max_loss_quote < 0 || limits.max_drawdown_quote < 0 || limits.max_orders_per_minute < 0)
        throw std::invalid_argument("risk limits must not be negative");
}

RiskGuard::RiskGuard(RiskLimits limits, std::int64_t initial_equity_quote)
    : limits_(limits), initial_(initial_equity_quote), peak_(initial_equity_quote)
{
    validate_limits(limits);
    if (initial_equity_quote < 0) throw std::invalid_argument("initial equity must not be negative");
}

void RiskGuard::enter(TradingState state, RiskTrigger trigger, std::optional<std::int64_t> equity)
{
    state_ = state;
    trigger_ = trigger;
    triggered_ns_ = now_ns_;
    trigger_equity_ = equity;
}

void RiskGuard::observe(std::int64_t now_ns, std::optional<std::int64_t> equity_quote)
{
    if (now_ns < now_ns_) throw std::runtime_error("risk time must not go backwards");
    now_ns_ = now_ns;
    // Without a trusted valuation nothing can be judged; the last known state stays.
    if (!equity_quote) return;
    const auto equity = *equity_quote;
    last_equity_ = equity;
    if (equity > peak_) peak_ = equity;
    if (state_ != TradingState::normal) return;
    // Equity may be far below zero only in theory; signed arithmetic keeps it checked.
    if (limits_.max_loss_quote > 0 && signed_subtract(initial_, equity) >= limits_.max_loss_quote)
        enter(TradingState::reduce_only, RiskTrigger::max_loss, equity);
    else if (limits_.max_drawdown_quote > 0 && signed_subtract(peak_, equity) >= limits_.max_drawdown_quote)
        enter(TradingState::reduce_only, RiskTrigger::max_drawdown, equity);
}

std::optional<std::string> RiskGuard::admit(const Order& order)
{
    if (limits_.max_orders_per_minute > 0 && state_ != TradingState::halted) {
        while (!recent_requests_.empty() && recent_requests_.front() <= now_ns_ - minute_ns) recent_requests_.pop_front();
        recent_requests_.push_back(now_ns_);
        if (static_cast<std::int64_t>(recent_requests_.size()) > limits_.max_orders_per_minute)
            enter(TradingState::halted, RiskTrigger::order_rate, last_equity_);
    }
    if (state_ == TradingState::halted)
        return "risk: trading halted (" + trigger_text(trigger_) + ")";
    if (state_ == TradingState::reduce_only && order.side == Side::buy)
        return "risk: reduce-only after " + trigger_text(trigger_) + "; buys are blocked";
    return std::nullopt;
}

} // namespace tickforge::execution
