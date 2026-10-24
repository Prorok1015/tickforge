#include <tickforge/console/console.hpp>
#include <cstdio>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

int main()
{
    using namespace tickforge;
    try {
        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        check(price_text(maximum) == "92233720368547758.07" &&
              quantity_text(maximum) == "92233720368.54775807", "display must preserve all decimal digits");
        check(price_text(1) == "0.01" && quantity_text(1) == "0.00000001", "display must retain leading zeros");
        check(price_text(0) == "0.00" && quantity_text(0) == "0.00000000", "zero formatting must be exact");
        std::fputs("Console formatting checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Console check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
