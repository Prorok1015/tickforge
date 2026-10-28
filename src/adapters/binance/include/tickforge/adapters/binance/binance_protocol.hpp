#pragma once

#include <tickforge/core/events.hpp>
#include <string_view>
#include <variant>

namespace tickforge::adapters::binance {

struct InstrumentRules {
    std::int64_t price_tick_atoms;
    std::int64_t quantity_step_atoms;
};

std::int64_t decimal_atoms(std::string_view text, unsigned decimals);
InstrumentRules parse_instrument(std::string_view json);
BookSnapshot parse_snapshot(std::string_view json, const InstrumentRules& rules);
BookUpdate parse_depth(std::string_view json, const InstrumentRules& rules, std::int64_t& exchange_ns);
// "trade" stream event; exchange_ns is the trade time T. "m" (buyer is maker) means the
// seller was the aggressor.
Trade parse_trade(std::string_view json, const InstrumentRules& rules, std::int64_t& exchange_ns);

// One message of the combined stream {"stream":..., "data":{...}}: depth or trade.
struct StreamMessage {
    std::variant<BookUpdate, Trade> data;
    std::int64_t exchange_ns;
};
StreamMessage parse_stream_message(std::string_view json, const InstrumentRules& rules);
// True for a serverShutdown notice, bare or inside a combined-stream wrapper.
bool is_server_shutdown(std::string_view json);

} // namespace tickforge::adapters::binance
