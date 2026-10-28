#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/core/book_tracker.hpp>
#include <deque>
#include <map>
#include <optional>
#include <string_view>
#include <variant>

namespace tickforge::execution {

// Simulated venue for one account. Market orders and the crossing part of limit orders
// take known depth (consumed virtually). The rest of a limit order rests at its price
// behind everything already visible there and is filled only by recorded trades:
// - an opposite-aggressor trade at our price first consumes the queue ahead of us;
// - a trade at a worse price than ours (a trade-through) fills us completely;
// - a book whose opposite side crosses our price fills us from that crossing size;
// - a level shrinking without trades is assumed to be cancellations behind us, so the
//   queue ahead only shrinks to the level's new size.
// Our orders are invisible to the recorded market; see docs/paper-trading.md.
class Simulator final : public Execution {
public:
    explicit Simulator(PaperConfig config);
    void on_market_event(const MarketEvent& event);
    bool available() const override;
    // An order submitted at session time t reaches the venue at t + order_latency_ns
    // and meets the book after the last event at or before that time: due actions run
    // when the first later event arrives, before it is applied. Zero latency acts at once.
    // Without a trusted, fresh book on arrival the order is cancelled.
    void submit(const Order& order) override;
    void cancel(std::int64_t order_id) override;
    // Reports whose report time (execution + report_latency_ns) has been reached.
    std::vector<ExecutionReport> take_reports() override;
    // End of market data: cancel orders still in flight or resting and release every
    // pending report, since executed fills did happen even if reported later.
    void finish();
    // Prices a market order against the current book and consumes virtual liquidity.
    ExecutionResult execute(const Order& order);
    const OrderBook& book() const { return tracker_.view(); }
    // Best level an order on `side` would hit now, net of this run's virtual
    // consumption: asks for buys, bids for sells. nullopt when nothing is left.
    std::optional<Level> executable_level(Side side) const;
    // Known size ahead of a resting order in its price queue (diagnostics, tests).
    std::optional<std::int64_t> queue_ahead(std::int64_t order_id) const;
private:
    struct Resting {
        Order order;
        std::int64_t remaining;
        std::int64_t queue_ahead;
        std::int64_t budget_left; // buys: unspent part of the reservation
        bool filled_any = false;
        std::int64_t entry_queue = 0; // diagnostics: visible size ahead when resting began
    };
    struct Action { std::variant<Order, std::int64_t> what; std::int64_t due_ns; }; // submit or cancel
    bool available_at(std::int64_t time) const;
    void run_due(std::int64_t before_ns);
    void arrive(const Order& order, std::int64_t time);
    void cancel_resting(std::int64_t order_id, std::int64_t time, Interruption why);
    void interrupt(const Order& order, std::int64_t time, Interruption why);
    ExecutionResult take(const Order& order, std::int64_t time, std::optional<std::int64_t> price_cap);
    void match_trade(const Trade& trade, std::int64_t time);
    void match_book(std::int64_t time);
    void fill_resting(Resting& resting, std::int64_t quantity, std::vector<Fill>& fills);
    void report_fills(Resting& resting, std::vector<Fill> fills, std::int64_t time, std::string_view done, FillSource source);
    void report(ExecutionResult result, std::int64_t time, Interruption why = Interruption::none,
                std::vector<FillDiagnostic> diagnostics = {});
    std::vector<FillDiagnostic> diagnose(const std::vector<Fill>& fills, FillSource source, std::int64_t time,
                                         const Resting* resting) const;
    void note_trade_id(const Trade& trade);
    std::int64_t level_size(Side side, std::int64_t price) const;
    PaperConfig config_;
    BookTracker tracker_;
    std::map<std::int64_t, std::int64_t> consumed_bids_, consumed_asks_;
    std::optional<std::int64_t> last_fresh_ns_;
    std::int64_t now_ns_ = 0;
    std::int64_t ordinal_ = 0;
    std::int64_t next_fill_id_ = 1;
    std::optional<std::int64_t> last_trade_id_;
    std::optional<std::int64_t> last_gap_ns_; // diagnostics: last trade ID gap or loss of book sync
    bool synchronized_ = false;
    std::deque<Action> in_flight_; // due times are nondecreasing: fixed latency, monotonic time
    std::vector<Resting> resting_; // arrival order
    std::vector<ExecutionReport> reports_;
    bool finished_ = false;
};

} // namespace tickforge::execution
