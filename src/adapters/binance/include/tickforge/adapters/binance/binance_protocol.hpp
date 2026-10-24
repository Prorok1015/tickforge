#pragma once

#include <tickforge/core/events.hpp>
#include <string_view>

namespace tickforge::adapters::binance {

struct InstrumentRules {
    std::int64_t price_tick_atoms;
    std::int64_t quantity_step_atoms;
};

std::int64_t decimal_atoms(std::string_view text, unsigned decimals);
InstrumentRules parse_instrument(std::string_view json);
BookSnapshot parse_snapshot(std::string_view json, const InstrumentRules& rules);
BookUpdate parse_depth(std::string_view json, const InstrumentRules& rules, std::int64_t& exchange_ns);
bool is_server_shutdown(std::string_view json);

} // namespace tickforge::adapters::binance
