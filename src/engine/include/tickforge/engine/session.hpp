#pragma once
#include <tickforge/core/events.hpp>
#include <tickforge/accounting/account.hpp>
#include <tickforge/execution/order_manager.hpp>
#include <tickforge/execution/risk.hpp>
#include <tickforge/execution/simulator.hpp>
#include <tickforge/strategy/strategy.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace tickforge::engine {

struct SessionConfig {
    execution::PaperConfig execution;
    execution::RiskLimits risk;
    std::int64_t initial_quote_atoms = 0;
    std::int64_t initial_base_atoms = 0;
    std::int64_t initial_base_cost_atoms = 0; // declared cost of the initial BTC
};

// Last best bid observed while execution was available: the valuation price.
struct Mark { std::int64_t price_atoms; std::int64_t ordinal; };

// What happened during a session, for presentation and reports. Callbacks run
// synchronously inside the session; they must not call back into it.
class SessionObserver {
public:
    virtual ~SessionObserver() = default;
    // After the event is applied and its due reports are committed, before decisions.
    // bid/ask are the recorded best levels while execution is available, else nullopt.
    virtual void on_market(const MarketEvent& /*event*/, bool /*available*/, std::optional<Level> /*bid*/,
                           std::optional<Level> /*ask*/) {}
    // first: the order's first report; signal names who asked for the order.
    virtual void on_report(const execution::OrderReport& /*report*/, const EventTime& /*submitted*/,
                           std::string_view /*signal*/, bool /*first*/) {}
    // Every request, before the order manager sees it.
    virtual void on_request(const EventTime& /*time*/, const Order& /*order*/) {}
    // An accepted order now travels to the venue; its reports follow later.
    virtual void on_accepted(const EventTime& /*time*/, const Order& /*order*/) {}
    // The risk state escalated; `cancelled` resting orders were asked to cancel.
    virtual void on_risk(const EventTime& /*time*/, std::size_t /*cancelled*/) {}
    virtual void on_signal(const EventTime& /*time*/, const strategy::Place& /*place*/) {}
    virtual void on_cancel(const EventTime& /*time*/, std::int64_t /*order_id*/, bool /*requested*/) {}
    virtual void on_notice(const EventTime& /*time*/, std::string_view /*text*/) {}
};

// One account trading on one market data stream: the loop shared by every mode.
// Market data is pushed in event order; nothing here reads a clock, so a recorded
// session replayed with the same config reproduces a live run exactly.
// Per event: simulator -> reports -> valuation -> risk -> strategy -> orders.
class Session {
public:
    // strategy may be null: orders then come only from submit() between events.
    Session(SessionConfig config, std::unique_ptr<strategy::Strategy> strategy, SessionObserver& observer);
    void on_event(const MarketEvent& event);
    // Requests from outside the strategy (scripts), handled at the last event's time.
    void submit(const Order& order, std::string_view signal);
    bool cancel(std::int64_t order_id);
    // End of market data: resting and in-flight orders end as session_ended, and
    // executed fills still report.
    void finish();

    const accounting::Account& account() const { return account_; }
    const execution::RiskGuard& risk() const { return risk_; }
    std::optional<Mark> mark() const { return mark_; }
    std::int64_t initial_equity() const { return initial_equity_; }
private:
    void submit_at(const EventTime& time, const Order& order, std::string_view signal);
    void collect();
    void note_risk(const EventTime& time);
    void decide(const EventTime& time);

    struct Submitted { EventTime time; std::string_view signal; bool reported = false; };
    SessionConfig config_;
    std::unique_ptr<strategy::Strategy> strategy_;
    SessionObserver& observer_;
    accounting::Account account_;
    execution::Simulator simulator_;
    std::int64_t initial_equity_;
    execution::RiskGuard risk_;
    execution::OrderManager manager_;
    execution::TradingState risk_state_ = execution::TradingState::normal;
    std::optional<Mark> mark_;
    std::optional<EventTime> last_time_;
    std::map<std::int64_t, Submitted> in_flight_; // accepted orders until their final report
};

} // namespace tickforge::engine
