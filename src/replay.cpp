#include "replay.hpp"

#include <charconv>
#include <format>
#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace tickforge {
namespace {
std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> result;
    for (;;) {
        const auto separator = line.find(',');
        result.push_back(line.substr(0, separator));
        if (separator == std::string_view::npos) return result;
        line.remove_prefix(separator + 1);
    }
}

std::int64_t integer(std::string_view value)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string_view::npos)
        throw std::runtime_error("expected decimal digits");
    std::int64_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() || result < 0)
        throw std::runtime_error("expected nonnegative int64");
    return result;
}

bool read_line(std::istream& input, std::string& line)
{
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

void levels(const std::vector<std::string_view>& values, std::size_t offset,
            std::vector<Level>& bids, std::vector<Level>& asks)
{
    if (values.size() < offset + 2) throw std::runtime_error("missing level counts");
    const auto bid_count = integer(values[offset]);
    const auto ask_count = integer(values[offset + 1]);
    const auto available = (values.size() - offset - 2) / 2;
    // Bound counts before adding/multiplying untrusted input.
    if (static_cast<std::uint64_t>(bid_count) > available ||
        static_cast<std::uint64_t>(ask_count) > available ||
        static_cast<std::uint64_t>(bid_count) + static_cast<std::uint64_t>(ask_count) != available ||
        (values.size() - offset - 2) % 2 != 0)
        throw std::runtime_error("level counts do not match price/quantity pairs");
    std::size_t cursor = offset + 2;
    for (auto* side : {&bids, &asks}) {
        const auto count = side == &bids ? bid_count : ask_count;
        for (std::int64_t i = 0; i < count; ++i) {
            const auto price = integer(values[cursor++]);
            const auto quantity = integer(values[cursor++]);
            if (price == 0) throw std::runtime_error("level price must be positive");
            side->push_back({price, quantity});
        }
    }
}
} // namespace

void replay(std::istream& input, const EventHandler& handler)
{
    std::string line;
    if (!read_line(input, line)) throw std::runtime_error("missing session header");
    const bool version3 =
        line == "tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000";
    const bool version2 = version3 ||
        line == "tickforge,2,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000";
    if (!version2 &&
        line != "tickforge,1,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000")
        throw std::runtime_error("invalid or unsupported session header");

    std::int64_t previous_ordinal = 0;
    std::int64_t previous_elapsed = 0;
    std::size_t line_number = 1;
    bool ended = false;
    while (read_line(input, line)) {
        ++line_number;
        MarketEvent event{};
        try {
            const auto values = fields(line);
            if (version3 && values.size() == 2 && values[0] == "end") {
                if (integer(values[1]) != previous_ordinal)
                    throw std::runtime_error("session footer event count mismatch");
                ended = true;
                break;
            }
            if (values.size() < 5) throw std::runtime_error("missing event metadata");
            event.time = {integer(values[1]), integer(values[2]), integer(values[3]), integer(values[4])};
            if (values[0] == "trade" && values.size() == 7) {
                const Trade trade{integer(values[5]), integer(values[6])};
                if (trade.price_atoms == 0 || trade.quantity_atoms == 0)
                    throw std::runtime_error("trade price and quantity must be positive");
                event.data = trade;
            } else if (version2 && (values[0] == "snapshot" ||
                       (version3 && values[0] == "snapshot_pending")) && values.size() >= 8) {
                BookSnapshot snapshot{integer(values[5]), {}, {}, values[0] == "snapshot_pending"};
                levels(values, 6, snapshot.bids, snapshot.asks);
                event.data = std::move(snapshot);
            } else if (version2 && values[0] == "depth" && values.size() >= 9) {
                BookUpdate update{integer(values[5]), integer(values[6]), {}, {}};
                if (update.first_update_id > update.last_update_id)
                    throw std::runtime_error("depth update ID range is reversed");
                levels(values, 7, update.bids, update.asks);
                event.data = std::move(update);
            } else if (version3 && values[0] == "reset" && values.size() == 6 &&
                       !values[5].empty() && values[5].find_first_not_of("abcdefghijklmnopqrstuvwxyz_") == std::string_view::npos) {
                event.data = BookReset{std::string(values[5])};
            } else {
                throw std::runtime_error("unsupported event type or field count for session version");
            }
            if (previous_ordinal == INT64_MAX || event.time.ordinal != previous_ordinal + 1)
                throw std::runtime_error("noncontiguous local ordinal");
            if (event.time.receive_elapsed_ns < previous_elapsed)
                throw std::runtime_error("monotonic receive time moved backwards");
        } catch (const std::runtime_error& error) {
            throw std::runtime_error(std::format("line {}: {}", line_number, error.what()));
        }
        previous_ordinal = event.time.ordinal;
        previous_elapsed = event.time.receive_elapsed_ns;
        handler(event);
    }
    if (version3 && (!ended || read_line(input, line)))
        throw std::runtime_error("incomplete session or data after footer");
    if (input.bad() || (input.fail() && !input.eof()))
        throw std::runtime_error("session read failed");
}
} // namespace tickforge
