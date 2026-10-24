#pragma once

#include <tickforge/core/book_tracker.hpp>
#include <string_view>

namespace tickforge {

class ReplayReporter {
public:
    explicit ReplayReporter(std::string_view source);
    void on_event(const MarketEvent& event);
    void summary() const;
private:
    void describe(const Trade& trade);
    void describe(const BookSnapshot& snapshot);
    void describe(const BookUpdate& update);
    void describe(const BookReset& reset);
    void status(BookResult result);

    BookTracker tracker_;
    std::size_t events_ = 0, trades_ = 0, snapshots_ = 0, updates_ = 0, resets_ = 0, warnings_ = 0;
};

} // namespace tickforge
