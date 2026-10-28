#pragma once
#include <tickforge/core/events.hpp>
#include <tickforge/core/orders.hpp>
#include <tickforge/execution/execution.hpp>
#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace tickforge::app {
// How much to trust simulated fills: where they came from and under which conditions.
struct FillStats {
    // A fill this close after a market-data gap may rest on missing trades or depth.
    static constexpr std::int64_t near_gap_ns = 1000000000;
    std::array<std::size_t, 4> by_source{}; // indexed by execution::FillSource
    std::size_t near_gap = 0;
    std::int64_t max_level_share_ppm = 0;   // resting fills only
    void add(const execution::FillDiagnostic& diagnostic);
};

inline constexpr std::array<std::int64_t, 3> markout_horizons_ns{1000000000, 10000000000, 60000000000};

// Value change of one fill after each horizon, in quote atoms for the fill quantity,
// positive when the market moved in our favour; nullopt when the data ended first.
struct MarkoutRow {
    Fill fill;
    std::int64_t executed_ns;
    std::array<std::optional<std::int64_t>, 3> quote_atoms{};
};

struct MarkoutTotal {
    std::size_t fills = 0;
    std::int64_t quantity_atoms = 0;
    std::int64_t quote_atoms = 0;
};

// Markout: the first trusted mid price at or after fill time + horizon, compared with
// the fill price. It reads later market data, so it is a report-only measure and must
// never reach a strategy. A report delayed past a horizon is measured when it arrives.
class Markouts {
public:
    void fill(const Fill& fill, std::int64_t executed_ns);
    // Best bid and ask of a trusted book at session time now_ns; nullopt when untrusted.
    void observe(std::int64_t now_ns, std::optional<Level> bid, std::optional<Level> ask);
    // Rows with every horizon measured, in fill order; with ended, all remaining rows.
    std::vector<MarkoutRow> take_done(bool ended = false);
    const MarkoutTotal& total(Liquidity liquidity, std::size_t horizon) const;
private:
    std::deque<MarkoutRow> pending_;
    std::array<std::array<MarkoutTotal, 3>, 2> totals_{}; // [taker, maker][horizon]
};
} // namespace tickforge::app
