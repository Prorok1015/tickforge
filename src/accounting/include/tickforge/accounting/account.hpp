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

// Average-cost position. Buy fees are part of the cost; sell fees reduce proceeds,
// so realized PnL is net of all commissions. Amounts are 1e-8 USDT.
struct Position {
    std::int64_t base_cost_quote = 0;    // cost of the BTC still held
    std::int64_t realized_pnl_quote = 0; // signed; changes only on sells
    bool operator==(const Position&) const = default;
};

class Account {
public:
    // Initial BTC needs an explicit cost basis; it must be zero without BTC.
    Account(std::int64_t quote_atoms, std::int64_t base_atoms, std::int64_t base_cost_quote_atoms);
    const Balances& balances() const { return balances_; }
    const Position& position() const { return position_; }
    std::int64_t pending_buy_quantity() const;
    void reserve(const Order& order);
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
    Position position_;
    std::map<std::int64_t, Reservation> orders_;
    std::map<std::int64_t, Fill> fills_;
};

} // namespace tickforge::accounting
