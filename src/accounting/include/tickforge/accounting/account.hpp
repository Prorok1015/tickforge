#pragma once

#include <tickforge/core/orders.hpp>
#include <map>

namespace tickforge::accounting {

struct Balances {
    std::int64_t quote_total = 0;
    std::int64_t quote_reserved = 0;
    std::int64_t base_total = 0;
    std::int64_t base_reserved = 0;
    std::int64_t available_quote() const { return quote_total - quote_reserved; }
    std::int64_t available_base() const { return base_total - base_reserved; }
    bool operator==(const Balances&) const = default;
};

class Account {
public:
    Account(std::int64_t quote_atoms, std::int64_t base_atoms);
    const Balances& balances() const { return balances_; }
    std::int64_t pending_buy_quantity() const;
    void reserve(const MarketOrder& order);
    bool apply_fill(const Fill& fill); // false for an identical already-applied fill
    bool release(std::int64_t order_id); // idempotent terminal release
private:
    struct Reservation {
        Side side;
        std::int64_t quantity_remaining;
        std::int64_t quote_remaining;
        bool closed = false;
    };
    Balances balances_;
    std::map<std::int64_t, Reservation> orders_;
    std::map<std::int64_t, Fill> fills_;
};

} // namespace tickforge::accounting
