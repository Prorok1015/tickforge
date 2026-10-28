#pragma once
#include <tickforge/core/orders.hpp>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace tickforge::execution {

// Account-level limits that apply to every strategy. Zero disables a limit.
struct RiskLimits {
    std::int64_t max_loss_quote = 0;      // equity below the session start by this much
    std::int64_t max_drawdown_quote = 0;  // equity below its session peak by this much
    std::int64_t max_orders_per_minute = 0; // requests in any 60 s of session time
};
void validate_limits(const RiskLimits& limits);

// normal -> reduce_only (losses: only sells, so a position can still be closed)
// any -> halted (runaway request rate: a sign of a broken strategy, so nothing trades).
// States never relax during a session: a limit that resets itself is not a limit.
enum class TradingState { normal, reduce_only, halted };
enum class RiskTrigger { none, max_loss, max_drawdown, order_rate };
std::string_view trigger_name(RiskTrigger trigger); // "none", "max_loss", ...

class RiskGuard {
public:
    // Initial equity: starting USDT plus the declared cost of starting BTC.
    RiskGuard(RiskLimits limits, std::int64_t initial_equity_quote);
    // Advances session time; equity is given when a trusted valuation exists.
    void observe(std::int64_t now_ns, std::optional<std::int64_t> equity_quote);
    // Pre-trade check for every request; counts it for the rate limit. Returns the
    // rejection reason, or nullopt to let the order continue to the other checks.
    std::optional<std::string> admit(const MarketOrder& order);
    TradingState state() const { return state_; }
    RiskTrigger trigger() const { return trigger_; }
    bool buys_allowed() const { return state_ == TradingState::normal; }
    bool sells_allowed() const { return state_ != TradingState::halted; }
    // Session time and equity (if known) when the current state was entered.
    std::optional<std::int64_t> triggered_ns() const { return triggered_ns_; }
    std::optional<std::int64_t> trigger_equity() const { return trigger_equity_; }
    std::int64_t peak_equity() const { return peak_; }
private:
    void enter(TradingState state, RiskTrigger trigger, std::optional<std::int64_t> equity);
    RiskLimits limits_;
    std::int64_t initial_;
    std::int64_t peak_;
    std::int64_t now_ns_ = 0;
    std::deque<std::int64_t> recent_requests_;
    TradingState state_ = TradingState::normal;
    RiskTrigger trigger_ = RiskTrigger::none;
    std::optional<std::int64_t> triggered_ns_, trigger_equity_, last_equity_;
};

} // namespace tickforge::execution
