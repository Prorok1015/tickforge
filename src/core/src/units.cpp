#include <tickforge/core/units.hpp>
#include <tickforge/core/amounts.hpp>
#include <format>
#include <stdexcept>

namespace tickforge {
namespace {
bool digits(std::string_view text)
{
    return !text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos;
}

std::int64_t power_of_ten(int exponent)
{
    std::int64_t value = 1;
    for (int i = 0; i < exponent; ++i) value = amount_multiply(value, 10);
    return value;
}
} // namespace

std::int64_t parse_decimal(std::string_view text, int fraction_digits)
{
    if (fraction_digits < 0 || fraction_digits > 18) throw std::invalid_argument("unsupported decimal scale");
    const auto point = text.find('.');
    const auto whole = text.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    if (!digits(whole) || (point != std::string_view::npos && !digits(fraction)))
        throw std::invalid_argument(std::format("'{}' is not a plain decimal number", text));
    if (fraction.size() > static_cast<std::size_t>(fraction_digits))
        throw std::invalid_argument(std::format("'{}' has more than {} fraction digits", text, fraction_digits));
    try {
        std::int64_t result = 0;
        for (const char c : whole) result = amount_add(amount_multiply(result, 10), c - '0');
        std::int64_t part = 0;
        for (const char c : fraction) part = amount_add(amount_multiply(part, 10), c - '0');
        const auto padding = fraction_digits - static_cast<int>(fraction.size());
        return amount_add(amount_multiply(result, power_of_ten(fraction_digits)), amount_multiply(part, power_of_ten(padding)));
    } catch (const std::overflow_error&) {
        throw std::invalid_argument(std::format("'{}' is too large", text));
    }
}

std::int64_t parse_price(std::string_view text) { return parse_decimal(text, 2); }
std::int64_t parse_quantity(std::string_view text) { return parse_decimal(text, 8); }
std::int64_t parse_quote(std::string_view text) { return parse_decimal(text, 8); }

std::int64_t parse_percent_ppm(std::string_view text)
{
    if (text.empty() || text.back() != '%') throw std::invalid_argument(std::format("'{}' must end with %", text));
    // 1% = 10000 ppm, so ppm are percent with four fraction digits.
    return parse_decimal(text.substr(0, text.size() - 1), 4);
}

std::int64_t parse_duration_ns(std::string_view text)
{
    struct Unit { std::string_view suffix; int fraction_digits; std::int64_t multiplier; };
    // Longest suffixes first so that "ms" is not read as "s".
    constexpr Unit units[] = {{"min", 9, 60}, {"ns", 0, 1}, {"us", 3, 1}, {"ms", 6, 1}, {"s", 9, 1}};
    for (const auto& unit : units) {
        if (text.size() > unit.suffix.size() && text.ends_with(unit.suffix)) {
            const auto value = parse_decimal(text.substr(0, text.size() - unit.suffix.size()), unit.fraction_digits);
            try {
                return amount_multiply(value, unit.multiplier);
            } catch (const std::overflow_error&) {
                throw std::invalid_argument(std::format("'{}' is too large", text));
            }
        }
    }
    throw std::invalid_argument(std::format("'{}' needs a unit: ns, us, ms, s, or min", text));
}
} // namespace tickforge
