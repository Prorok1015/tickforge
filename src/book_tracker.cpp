#include "book_tracker.hpp"
#include <stdexcept>

namespace tickforge {

void BookTracker::buffer(const BookUpdate& update)
{
    const auto levels = update.bids.size() + update.asks.size();
    if (buffered_.size() >= 4096 || levels > 200000 - buffered_levels_)
        throw std::runtime_error("snapshot synchronization buffer limit exceeded");
    buffered_.push_back(update);
    buffered_levels_ += levels;
}

BookResult BookTracker::apply(const BookReset&)
{
    book_ = OrderBook{};
    buffered_.clear();
    buffered_levels_ = 0;
    live_ = true;
    ready_ = false;
    have_snapshot_ = false;
    return BookResult::reset;
}

BookResult BookTracker::apply(const BookSnapshot& snapshot)
{
    live_ = snapshot.requires_bridge;
    const auto result = book_.apply(snapshot);
    ready_ = false;
    have_snapshot_ = result == BookResult::snapshot;
    if (!have_snapshot_) {
        buffered_.clear();
        buffered_levels_ = 0;
        return result;
    }
    if (!live_) {
        ready_ = true;
        buffered_.clear();
        buffered_levels_ = 0;
        return result;
    }
    auto pending = std::move(buffered_);
    buffered_.clear();
    buffered_levels_ = 0;
    for (const auto& update : pending) {
        const auto applied = book_.apply(update);
        if (applied == BookResult::gap || applied == BookResult::invalid) {
            ready_ = false;
            have_snapshot_ = false;
            // A later REST snapshot supersedes the discarded updates.
            return applied;
        }
        if (applied == BookResult::applied) ready_ = true;
    }
    return ready_ ? BookResult::snapshot : BookResult::awaiting_bridge;
}

BookResult BookTracker::apply(const BookUpdate& update)
{
    if (live_ && !have_snapshot_) {
        buffer(update);
        return BookResult::buffering;
    }
    const auto result = book_.apply(update);
    if (result == BookResult::applied) ready_ = true;
    if (result == BookResult::gap || result == BookResult::invalid) {
        ready_ = false;
        have_snapshot_ = false;
        if (live_) {
            buffered_.clear();
            buffered_levels_ = 0;
            buffer(update);
        }
    }
    return result;
}
} // namespace tickforge
