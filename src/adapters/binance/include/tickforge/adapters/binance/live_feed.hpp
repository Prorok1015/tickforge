#pragma once
#include <tickforge/core/events.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace tickforge::adapters::binance {

struct FeedOptions {
    unsigned seconds = 0;                   // wall-clock duration including connection
    std::optional<std::string> record_path; // a new session file, completed with a footer
    bool show_book = false;                 // print the book once per second (capture view)
};

struct FeedSummary {
    std::int64_t events = 0;
    std::int64_t trades = 0;
    unsigned reconnects = 0;
};

// Public BTCUSDT depth and trades as normalized market events. No authentication or
// order-submission code exists. Every event is recorded first (when recording), then
// handed to `on_event`, so a recording holds exactly what the handler saw. The feed
// ends after the duration or on SIGINT/SIGTERM with a final BookReset{"shutdown"}
// event; a throw from the handler stops it without a recording footer.
// `on_note` receives connection status lines (metadata, snapshots, reconnects).
FeedSummary run_feed(const FeedOptions& options, const std::function<void(const MarketEvent&)>& on_event,
                     const std::function<void(std::string_view)>& on_note);

} // namespace tickforge::adapters::binance
