#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tickforge {

struct EventTime {
    std::int64_t ordinal;
    std::int64_t exchange_ns;
    std::int64_t receive_unix_ns;
    std::int64_t receive_elapsed_ns;
};

struct Trade {
    std::int64_t price_atoms;
    std::int64_t quantity_atoms;
};

struct Level {
    std::int64_t price_atoms;
    std::int64_t quantity_atoms;
};

struct BookSnapshot {
    std::int64_t last_update_id;
    std::vector<Level> bids;
    std::vector<Level> asks;
    bool requires_bridge = false;
};

// Local lifecycle event: quotes must not survive a broken connection.
struct BookReset {
    std::string reason;
};

struct BookUpdate {
    std::int64_t first_update_id;
    std::int64_t last_update_id;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

struct MarketEvent {
    EventTime time;
    std::variant<Trade, BookSnapshot, BookUpdate, BookReset> data;
};

} // namespace tickforge
