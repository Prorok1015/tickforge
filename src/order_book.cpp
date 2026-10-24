#include "order_book.hpp"

namespace tickforge {
namespace {
bool valid_levels(const std::vector<Level>& levels, bool snapshot)
{
    for (const auto& level : levels)
        if (level.price_atoms <= 0 || level.quantity_atoms < 0 ||
            (snapshot && level.quantity_atoms == 0)) return false;
    return true;
}

void update_levels(std::map<std::int64_t, std::int64_t>& side, const std::vector<Level>& levels)
{
    for (const auto& level : levels) {
        if (level.quantity_atoms == 0) side.erase(level.price_atoms);
        else side[level.price_atoms] = level.quantity_atoms;
    }
}
} // namespace

void OrderBook::invalidate()
{
    bids_.clear();
    asks_.clear();
    synchronized_ = false;
    last_update_id_ = 0;
}

bool OrderBook::crossed() const
{
    return !bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first;
}

BookResult OrderBook::apply(const BookSnapshot& snapshot)
{
    invalidate();
    if (snapshot.last_update_id < 0 || !valid_levels(snapshot.bids, true) ||
        !valid_levels(snapshot.asks, true)) return BookResult::invalid;
    for (const auto& level : snapshot.bids) {
        if (!bids_.emplace(level.price_atoms, level.quantity_atoms).second) {
            invalidate();
            return BookResult::invalid;
        }
    }
    for (const auto& level : snapshot.asks) {
        if (!asks_.emplace(level.price_atoms, level.quantity_atoms).second) {
            invalidate();
            return BookResult::invalid;
        }
    }
    if (crossed()) {
        invalidate();
        return BookResult::invalid;
    }
    last_update_id_ = snapshot.last_update_id;
    synchronized_ = true;
    return BookResult::snapshot;
}

BookResult OrderBook::apply(const BookUpdate& update)
{
    if (update.first_update_id < 0 || update.last_update_id < update.first_update_id ||
        !valid_levels(update.bids, false) || !valid_levels(update.asks, false)) {
        invalidate();
        return BookResult::invalid;
    }
    if (!synchronized_) return BookResult::awaiting_snapshot;
    if (update.last_update_id <= last_update_id_) return BookResult::stale;
    // last_update_id_ cannot be INT64_MAX here: the new ID is strictly greater.
    if (update.first_update_id > last_update_id_ + 1) {
        invalidate();
        return BookResult::gap;
    }
    // Apply both sides before checking for a crossed book: an event is atomic.
    update_levels(bids_, update.bids);
    update_levels(asks_, update.asks);
    if (crossed()) {
        invalidate();
        return BookResult::invalid;
    }
    last_update_id_ = update.last_update_id;
    return BookResult::applied;
}

std::optional<std::int64_t> OrderBook::last_update_id() const
{
    if (!synchronized_) return std::nullopt;
    return last_update_id_;
}

std::optional<Level> OrderBook::best_bid() const
{
    if (!synchronized_ || bids_.empty()) return std::nullopt;
    return Level{bids_.rbegin()->first, bids_.rbegin()->second};
}

std::optional<Level> OrderBook::best_ask() const
{
    if (!synchronized_ || asks_.empty()) return std::nullopt;
    return Level{asks_.begin()->first, asks_.begin()->second};
}

std::optional<std::int64_t> OrderBook::spread_atoms() const
{
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid || !ask) return std::nullopt;
    return ask->price_atoms - bid->price_atoms;
}
} // namespace tickforge
