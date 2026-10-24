#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tickforge {

// BTC quantity: 1e-8 BTC; price: 1e-2 USDT; money/fees: 1e-8 USDT.
enum class Side { buy, sell };

struct MarketOrder {
    std::int64_t id;
    Side side;
    std::int64_t quantity_atoms;
    std::int64_t max_spend_quote_atoms; // Buy budget INCLUDING fees; zero for sells.
};

struct Fill {
    std::int64_t id;
    std::int64_t order_id;
    Side side;
    std::int64_t price_atoms;
    std::int64_t quantity_atoms;
    std::int64_t quote_atoms;
    std::int64_t fee_quote_atoms;
    bool operator==(const Fill&) const = default;
};

// Every result is terminal: any unfilled quantity is cancelled immediately.
enum class OrderStatus { rejected, filled, partially_filled, cancelled };
struct ExecutionResult {
    std::int64_t order_id;
    OrderStatus status;
    std::vector<Fill> fills;
    std::int64_t remaining_quantity_atoms;
    std::string reason;
};

} // namespace tickforge
