#pragma once
#include <cstdint>
#include <string_view>

namespace tickforge {
// Exact human-readable text -> integer atoms, without floating point.
// Accepted: digits with an optional fraction ("81000", "81000.5"). Rejected: signs,
// exponents, spaces, empty parts, more fraction digits than the unit can hold
// (no silent rounding), and values that do not fit int64.
std::int64_t parse_decimal(std::string_view text, int fraction_digits);

std::int64_t parse_price(std::string_view text);       // USDT per BTC -> 1e-2 USDT
std::int64_t parse_quantity(std::string_view text);    // BTC -> 1e-8 BTC
std::int64_t parse_quote(std::string_view text);       // USDT amount -> 1e-8 USDT
std::int64_t parse_percent_ppm(std::string_view text); // "0.1%" -> 1000 parts per million
// "100ns", "250us", "50ms", "1.5s", "2min" -> nanoseconds; the unit is required.
std::int64_t parse_duration_ns(std::string_view text);
} // namespace tickforge
