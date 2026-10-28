#include <tickforge/core/units.hpp>
#include <cstdio>
#include <format>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function, const char* message)
{
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}
}

int main()
{
    using namespace tickforge;
    try {
        check(parse_price("81000") == 8100000 && parse_price("81000.5") == 8100050 &&
              parse_price("81000.05") == 8100005 && parse_price("0.01") == 1, "prices in 1e-2 USDT");
        check(parse_quantity("0.01") == 1000000 && parse_quantity("0.00000001") == 1 &&
              parse_quantity("2") == 200000000, "BTC quantities in 1e-8 BTC");
        check(parse_quote("10000") == 1000000000000 && parse_quote("0.1") == 10000000, "USDT amounts in 1e-8 USDT");
        check(parse_percent_ppm("0.1%") == 1000 && parse_percent_ppm("100%") == 1000000 &&
              parse_percent_ppm("0.0001%") == 1 && parse_percent_ppm("0%") == 0, "percent to parts per million");
        check(parse_duration_ns("100ns") == 100 && parse_duration_ns("250us") == 250000 &&
              parse_duration_ns("50ms") == 50000000 && parse_duration_ns("1.5s") == 1500000000 &&
              parse_duration_ns("2min") == 120000000000 && parse_duration_ns("0s") == 0, "durations to nanoseconds");
        check(parse_decimal("9223372036854775807", 0) == 9223372036854775807, "int64 maximum is representable");

        // No silent rounding, no floating-point spellings, no signs or spaces.
        rejects([] { parse_price("81000.005"); }, "a third price decimal must be rejected");
        rejects([] { parse_quantity("0.000000001"); }, "a ninth BTC decimal must be rejected");
        rejects([] { parse_duration_ns("1.5ns"); }, "fractional nanoseconds must be rejected");
        rejects([] { parse_percent_ppm("0.00001%"); }, "below one ppm must be rejected");
        for (const auto* bad : {"", ".", "1.", ".5", "-1", "+1", "1e3", " 1", "1 ", "1,5", "0x10", "1.2.3"})
            rejects([&] { parse_price(bad); }, "malformed decimal must be rejected");
        rejects([] { parse_decimal("9223372036854775808", 0); }, "int64 overflow must be rejected");
        rejects([] { parse_quote("100000000000"); }, "overflow after scaling must be rejected");
        rejects([] { parse_percent_ppm("0.1"); }, "percent needs its sign");
        rejects([] { parse_duration_ns("50"); }, "a duration needs a unit");
        rejects([] { parse_duration_ns("ms"); }, "a duration needs a number");
        rejects([] { parse_duration_ns("1h"); }, "unknown units must be rejected");
        rejects([] { parse_duration_ns("200000000min"); }, "duration overflow must be rejected");
        std::fputs("Unit parsing checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Unit parsing check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
