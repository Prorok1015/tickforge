#include <tickforge/console/console.hpp>

#include <format>
#include <stdexcept>

namespace tickforge {

std::string price_text(std::int64_t atoms)
{
    if (atoms < 0) throw std::invalid_argument("negative display price");
    return std::format("{}.{:02}", atoms / 100, atoms % 100);
}

std::string quantity_text(std::int64_t atoms)
{
    if (atoms < 0) throw std::invalid_argument("negative display quantity");
    return std::format("{}.{:08}", atoms / 100000000, atoms % 100000000);
}

void write_console(std::string_view text, std::FILE* stream)
{
    if (std::fwrite(text.data(), 1, text.size(), stream) != text.size())
        throw std::runtime_error("console write failed");
}

std::string_view result_text(BookResult result)
{
    switch (result) {
    case BookResult::snapshot: return "SNAPSHOT | replaced local book";
    case BookResult::applied: return "APPLIED | absolute quantities updated";
    case BookResult::stale: return "IGNORED | update already covered";
    case BookResult::awaiting_snapshot: return "WAITING | depth ignored until a snapshot arrives";
    case BookResult::buffering: return "BUFFERING | depth retained while awaiting a REST snapshot";
    case BookResult::awaiting_bridge: return "WAITING | snapshot received; waiting for a bridging depth range";
    case BookResult::reset: return "RESET | quotes unavailable; connection lifecycle changed";
    case BookResult::gap: return "GAP | book cleared; fresh snapshot required";
    case BookResult::invalid: return "INVALID | book cleared; fresh snapshot required";
    }
    return "UNKNOWN";
}

std::string book_text(const OrderBook& book)
{
    if (!book.synchronized())
        return "  BOOK   | UNAVAILABLE | no trusted snapshot/update sequence\n";
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    const auto spread = book.spread_atoms();
    return std::format(
        "  BOOK   | synchronized at ID {} | levels: {} bid / {} ask\n"
        "  BID    | {}\n  ASK    | {}\n  SPREAD | {}\n",
        *book.last_update_id(), book.bid_levels(), book.ask_levels(),
        bid ? std::format("{} USDT x {} BTC", price_text(bid->price_atoms), quantity_text(bid->quantity_atoms))
            : "unavailable (no known bid levels)",
        ask ? std::format("{} USDT x {} BTC", price_text(ask->price_atoms), quantity_text(ask->quantity_atoms))
            : "unavailable (no known ask levels)",
        spread ? std::format("{} USDT (ask - bid)", price_text(*spread)) : "unavailable");
}
} // namespace tickforge
