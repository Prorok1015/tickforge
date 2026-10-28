#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tickforge {

// BTC quantity: 1e-8 BTC; price: 1e-2 USDT; money/fees: 1e-8 USDT.
enum class Side { buy, sell };

// market: executes against the book at once, any remainder is cancelled.
// limit: executes at the limit price or better; the remainder rests in the book and
// waits in the queue at that price until filled or cancelled.
enum class OrderType { market, limit };

struct Order {
    std::int64_t id;
    Side side;
    std::int64_t quantity_atoms;
    std::int64_t max_spend_quote_atoms; // Buy budget INCLUDING fees; zero for sells.
    OrderType type = OrderType::market;
    std::int64_t limit_price_atoms = 0; // limit orders only
    bool post_only = false;             // limit only: reject instead of crossing the spread
};

// taker: our order crossed the spread; maker: our resting order was hit by another trader.
enum class Liquidity { taker, maker };

struct Fill {
    std::int64_t id;
    std::int64_t order_id;
    Side side;
    std::int64_t price_atoms;
    std::int64_t quantity_atoms;
    std::int64_t quote_atoms;
    std::int64_t fee_quote_atoms;
    Liquidity liquidity = Liquidity::taker;
    bool operator==(const Fill&) const = default;
};

// open: the order rests in the book, possibly after some fills; more reports follow.
// Every other status is terminal: partially_filled means the unfilled remainder was
// cancelled (at once for market orders, by a cancel request for limit orders).
enum class OrderStatus { rejected, filled, partially_filled, cancelled, open };

// One execution report. fills holds only the fills new in this report;
// remaining_quantity_atoms is what is still unfilled after them.
struct ExecutionResult {
    std::int64_t order_id;
    OrderStatus status;
    std::vector<Fill> fills;
    std::int64_t remaining_quantity_atoms;
    std::string reason;
};

inline bool terminal(OrderStatus status) { return status != OrderStatus::open; }

} // namespace tickforge
