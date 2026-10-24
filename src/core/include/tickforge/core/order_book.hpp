#pragma once

#include <tickforge/core/events.hpp>
#include <map>
#include <optional>

namespace tickforge {

enum class BookResult { snapshot, applied, stale, awaiting_snapshot, buffering, awaiting_bridge, reset, gap, invalid };

class OrderBook {
public:
    BookResult apply(const BookSnapshot& snapshot);
    BookResult apply(const BookUpdate& update);
    bool synchronized() const { return synchronized_; }
    std::optional<std::int64_t> last_update_id() const;
    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;
    std::optional<std::int64_t> spread_atoms() const;
    std::size_t bid_levels() const { return bids_.size(); }
    std::size_t ask_levels() const { return asks_.size(); }

private:
    void invalidate();
    bool crossed() const;
    std::map<std::int64_t, std::int64_t> bids_;
    std::map<std::int64_t, std::int64_t> asks_;
    std::int64_t last_update_id_ = 0;
    bool synchronized_ = false;
};

} // namespace tickforge
