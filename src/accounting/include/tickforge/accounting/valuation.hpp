#pragma once
#include <tickforge/accounting/account.hpp>
#include <optional>

namespace tickforge::accounting {

// Mark-to-market at one price (1e-2 USDT per BTC), normally the best trusted bid:
// the price a market sale would start at. Exit fees and deeper levels are excluded.
struct Valuation {
    std::int64_t mark_price_atoms;
    std::int64_t market_value_quote; // held BTC at the mark, rounded down like a sale
    std::int64_t unrealized_pnl_quote; // signed: market value minus remaining cost
    std::int64_t total_pnl_quote;    // signed: realized plus unrealized
    std::int64_t equity_quote;       // USDT total plus market value
};

Valuation value_at(const Account& account, std::int64_t mark_price_atoms);

// Average cost in price atoms per BTC, rounded up; nullopt without BTC.
std::optional<std::int64_t> average_cost_price(const Balances& balances, const Position& position);

} // namespace tickforge::accounting
