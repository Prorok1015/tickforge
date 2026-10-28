#include "paper_diagnostics.hpp"
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <cstdlib>

namespace tickforge::app {
void FillStats::add(const execution::FillDiagnostic& diagnostic)
{
    ++by_source[static_cast<std::size_t>(diagnostic.source)];
    if (diagnostic.since_gap_ns && *diagnostic.since_gap_ns <= near_gap_ns) ++near_gap;
    max_level_share_ppm = std::max(max_level_share_ppm, diagnostic.level_share_ppm);
}

void Markouts::fill(const Fill& fill, std::int64_t executed_ns) { pending_.push_back({fill, executed_ns}); }

void Markouts::observe(std::int64_t now_ns, std::optional<Level> bid, std::optional<Level> ask)
{
    if (!bid || !ask) return;
    // Twice the mid keeps half ticks exact.
    const auto mid2 = amount_add(bid->price_atoms, ask->price_atoms);
    for (auto& row : pending_) {
        for (std::size_t h = 0; h < markout_horizons_ns.size(); ++h) {
            if (row.quote_atoms[h] || now_ns < amount_add(row.executed_ns, markout_horizons_ns[h])) continue;
            const auto& fill = row.fill;
            // A buy gains when the mid rises above our price, a sell when it falls below.
            const auto move2 = fill.side == Side::buy ? signed_subtract(mid2, amount_multiply(2, fill.price_atoms))
                                                      : signed_subtract(amount_multiply(2, fill.price_atoms), mid2);
            // Price atoms are 1e-2 USDT and quantity atoms 1e-8 BTC: quote = price * qty / 100.
            const auto size = amount_multiply_divide(std::llabs(move2), fill.quantity_atoms, 200, false);
            const auto value = move2 < 0 ? -size : size;
            row.quote_atoms[h] = value;
            auto& total = totals_[fill.liquidity == Liquidity::maker ? 1 : 0][h];
            ++total.fills;
            total.quantity_atoms = amount_add(total.quantity_atoms, fill.quantity_atoms);
            total.quote_atoms = signed_add(total.quote_atoms, value);
        }
    }
}

std::vector<MarkoutRow> Markouts::take_done(bool ended)
{
    std::vector<MarkoutRow> done;
    // Fills arrive in execution order, so the oldest rows finish first.
    while (!pending_.empty() && (ended || pending_.front().quote_atoms.back())) {
        done.push_back(pending_.front());
        pending_.pop_front();
    }
    return done;
}

const MarkoutTotal& Markouts::total(Liquidity liquidity, std::size_t horizon) const
{
    return totals_[liquidity == Liquidity::maker ? 1 : 0][horizon];
}
} // namespace tickforge::app
