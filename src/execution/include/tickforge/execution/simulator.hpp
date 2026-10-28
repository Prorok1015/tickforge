#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/core/book_tracker.hpp>
#include <deque>
#include <map>
#include <optional>

namespace tickforge::execution {

class Simulator final : public Execution {
public:
    explicit Simulator(PaperConfig config);
    void on_market_event(const MarketEvent& event);
    bool available() const override;
    // An order submitted at session time t executes at t + order_latency_ns against
    // the book after the last event at or before that time: due orders run when the
    // first later event arrives, before it is applied. Zero latency executes at once.
    // Without a trusted, fresh book at that time the order is cancelled.
    void submit(const MarketOrder& order) override;
    // Reports whose report time (execution + report_latency_ns) has been reached.
    std::vector<ExecutionReport> take_reports() override;
    // End of market data: cancel orders not yet executed and release every pending
    // report, since executed orders did trade even if the report time lies beyond.
    void finish();
    // Prices an order against the current book and consumes virtual liquidity.
    ExecutionResult execute(const MarketOrder& order);
    const OrderBook& book() const { return tracker_.view(); }
    // Best level an order on `side` would hit now, net of this run's virtual
    // consumption: asks for buys, bids for sells. nullopt when nothing is left.
    std::optional<Level> executable_level(Side side) const;
private:
    bool available_at(std::int64_t time) const;
    ExecutionResult execute_at(const MarketOrder& order, std::int64_t time);
    void run_due(std::int64_t before_ns);
    void cancel(const MarketOrder& order, std::int64_t time, Interruption why);
    struct InFlight { MarketOrder order; std::int64_t due_ns; };
    PaperConfig config_;
    BookTracker tracker_;
    std::map<std::int64_t, std::int64_t> consumed_bids_, consumed_asks_;
    std::optional<std::int64_t> last_fresh_ns_;
    std::int64_t now_ns_ = 0;
    std::int64_t ordinal_ = 0;
    std::int64_t next_fill_id_ = 1;
    std::deque<InFlight> in_flight_; // due times are nondecreasing: fixed latency, monotonic time
    std::vector<ExecutionReport> reports_;
    bool finished_ = false;
};

} // namespace tickforge::execution
