#include <tickforge/accounting/account.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>

namespace tickforge::accounting {

Account::Account(std::int64_t quote_atoms, std::int64_t base_atoms, std::int64_t base_cost_quote_atoms)
{
    if (quote_atoms < 0 || base_atoms < 0 || base_cost_quote_atoms < 0) throw std::invalid_argument("negative initial balance");
    if (base_atoms == 0 && base_cost_quote_atoms != 0) throw std::invalid_argument("cost basis without initial BTC");
    balances_.quote_total = quote_atoms;
    balances_.base_total = base_atoms;
    position_.base_cost_quote = base_cost_quote_atoms;
}

std::int64_t Account::pending_buy_quantity() const
{
    std::int64_t quantity = 0;
    for (const auto& [id, reservation] : orders_) {
        static_cast<void>(id);
        if (!reservation.closed && reservation.side == Side::buy)
            quantity = amount_add(quantity, reservation.quantity_remaining);
    }
    return quantity;
}

void Account::reserve(const MarketOrder& order)
{
    if (order.id <= 0 || order.quantity_atoms <= 0 ||
        (order.side != Side::buy && order.side != Side::sell) ||
        (order.side == Side::buy && order.max_spend_quote_atoms <= 0) ||
        (order.side == Side::sell && order.max_spend_quote_atoms != 0))
        throw std::invalid_argument("invalid reservation request");
    if (orders_.contains(order.id)) throw std::runtime_error("order ID already exists");
    auto balances = balances_;
    if (order.side == Side::buy) {
        if (order.max_spend_quote_atoms > balances.available_quote())
            throw std::runtime_error("insufficient available USDT");
        balances.quote_reserved = amount_add(balances.quote_reserved, order.max_spend_quote_atoms);
    } else {
        if (order.quantity_atoms > balances.available_base())
            throw std::runtime_error("insufficient available BTC");
        balances.base_reserved = amount_add(balances.base_reserved, order.quantity_atoms);
    }
    orders_.emplace(order.id, Reservation{order.side, order.quantity_atoms, order.max_spend_quote_atoms});
    balances_ = balances;
}

bool Account::apply_fill(const Fill& fill)
{
    if (const auto existing = fills_.find(fill.id); existing != fills_.end()) {
        if (existing->second == fill) return false;
        throw std::runtime_error("conflicting duplicate fill ID");
    }
    const auto found = orders_.find(fill.order_id);
    if (fill.id <= 0 || fill.price_atoms <= 0 || fill.quantity_atoms <= 0 ||
        fill.quote_atoms < 0 || (fill.side == Side::buy && fill.quote_atoms == 0) ||
        fill.fee_quote_atoms < 0 || found == orders_.end())
        throw std::runtime_error("invalid or unknown execution fill");
    auto reservation = found->second;
    if (reservation.closed || reservation.side != fill.side || fill.quantity_atoms > reservation.quantity_remaining)
        throw std::runtime_error("fill does not match an open order");
    auto balances = balances_;
    auto position = position_;
    if (fill.side == Side::buy) {
        const auto cost = amount_add(fill.quote_atoms, fill.fee_quote_atoms);
        reservation.quote_remaining = amount_subtract(reservation.quote_remaining, cost);
        balances.quote_reserved = amount_subtract(balances.quote_reserved, cost);
        balances.quote_total = amount_subtract(balances.quote_total, cost);
        balances.base_total = amount_add(balances.base_total, fill.quantity_atoms);
        position.base_cost_quote = amount_add(position.base_cost_quote, cost);
    } else {
        const auto proceeds = amount_subtract(fill.quote_atoms, fill.fee_quote_atoms);
        // The sold share of the average cost is rounded up: rounding never adds to
        // realized profit. Closing the whole position removes the exact remainder.
        const auto sold_cost = fill.quantity_atoms == balances.base_total ? position.base_cost_quote
            : amount_fraction(position.base_cost_quote, fill.quantity_atoms, balances.base_total, true);
        balances.base_reserved = amount_subtract(balances.base_reserved, fill.quantity_atoms);
        balances.base_total = amount_subtract(balances.base_total, fill.quantity_atoms);
        balances.quote_total = amount_add(balances.quote_total, proceeds);
        position.base_cost_quote = amount_subtract(position.base_cost_quote, sold_cost);
        position.realized_pnl_quote = signed_add(position.realized_pnl_quote, signed_subtract(proceeds, sold_cost));
    }
    reservation.quantity_remaining = amount_subtract(reservation.quantity_remaining, fill.quantity_atoms);
    // Allocation can throw; commit balances only after the receipt was stored.
    fills_.emplace(fill.id, fill);
    found->second = reservation;
    balances_ = balances;
    position_ = position;
    return true;
}

bool Account::release(std::int64_t order_id)
{
    const auto found = orders_.find(order_id);
    if (found == orders_.end()) throw std::runtime_error("cannot release unknown order");
    if (found->second.closed) return false;
    auto balances = balances_;
    if (found->second.side == Side::buy)
        balances.quote_reserved = amount_subtract(balances.quote_reserved, found->second.quote_remaining);
    else balances.base_reserved = amount_subtract(balances.base_reserved, found->second.quantity_remaining);
    found->second.quote_remaining = 0;
    found->second.closed = true;
    balances_ = balances;
    return true;
}
} // namespace tickforge::accounting
