#include <tickforge/accounting/valuation.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>

namespace tickforge::accounting {

Valuation value_at(const Account& account, std::int64_t mark_price_atoms)
{
    if (mark_price_atoms <= 0) throw std::invalid_argument("mark price must be positive");
    const auto& balances = account.balances();
    const auto& position = account.position();
    // price (1e-2 USDT) * quantity (1e-8 BTC) / 100 = quote (1e-8 USDT)
    const auto value = amount_multiply_divide(mark_price_atoms, balances.base_total, 100, false);
    const auto unrealized = signed_subtract(value, position.base_cost_quote);
    return {mark_price_atoms, value, unrealized, signed_add(position.realized_pnl_quote, unrealized),
            amount_add(balances.quote_total, value)};
}

std::optional<std::int64_t> average_cost_price(const Balances& balances, const Position& position)
{
    if (balances.base_total == 0) return std::nullopt;
    // cost (1e-8 USDT) * 100 / quantity (1e-8 BTC) = price (1e-2 USDT); split to avoid overflow.
    const auto whole = position.base_cost_quote / balances.base_total;
    const auto rest = position.base_cost_quote % balances.base_total;
    return amount_add(amount_multiply(whole, 100), amount_fraction(100, rest, balances.base_total, true));
}

} // namespace tickforge::accounting
