#pragma once

#include "order_book.hpp"
#include <deque>

namespace tickforge {

// Preserves receipt order: depth arriving before the REST snapshot is buffered
// here in both live mode and replay. No future event is needed to expose state.
class BookTracker {
public:
    BookResult apply(const BookReset& reset);
    BookResult apply(const BookSnapshot& snapshot);
    BookResult apply(const BookUpdate& update);
    const OrderBook& view() const { return ready_ ? book_ : unavailable_; }
    bool needs_snapshot() const { return live_ && !have_snapshot_; }
private:
    void buffer(const BookUpdate& update);
    OrderBook book_;
    OrderBook unavailable_;
    std::deque<BookUpdate> buffered_;
    std::size_t buffered_levels_ = 0;
    bool live_ = false;
    bool ready_ = false;
    bool have_snapshot_ = false;
};

} // namespace tickforge
