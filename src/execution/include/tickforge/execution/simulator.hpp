#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/core/book_tracker.hpp>
#include <map>
#include <optional>

namespace tickforge::execution {

class Simulator final : public Execution {
public:
    explicit Simulator(PaperConfig config);
    void on_market_event(const MarketEvent& event);
    bool available() const override;
    // Zero-latency model: an order executes against the current book at submission,
    // and its report is ready at once.
    void submit(const MarketOrder& order) override;
    std::vector<ExecutionReport> take_reports() override;
    // Prices an order against the current book and consumes virtual liquidity.
    ExecutionResult execute(const MarketOrder& order);
    const OrderBook& book() const { return tracker_.view(); }
    // Best level an order on `side` would hit now, net of this run's virtual
    // consumption: asks for buys, bids for sells. nullopt when nothing is left.
    std::optional<Level> executable_level(Side side) const;
private:
    PaperConfig config_;
    BookTracker tracker_;
    std::map<std::int64_t, std::int64_t> consumed_bids_, consumed_asks_;
    std::optional<std::int64_t> last_fresh_ns_;
    std::int64_t now_ns_ = 0;
    std::int64_t ordinal_ = 0;
    std::int64_t next_fill_id_ = 1;
    std::vector<ExecutionReport> reports_;
};

} // namespace tickforge::execution
