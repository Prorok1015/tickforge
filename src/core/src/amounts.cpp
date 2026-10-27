#include <tickforge/core/amounts.hpp>
#include <limits>
#include <stdexcept>

namespace tickforge {
namespace {
constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
void nonnegative(std::int64_t value)
{
    if (value < 0) throw std::invalid_argument("negative monetary/quantity amount");
}
}

std::int64_t amount_add(std::int64_t left, std::int64_t right)
{
    nonnegative(left); nonnegative(right);
    if (left > maximum - right) throw std::overflow_error("amount addition overflow");
    return left + right;
}

std::int64_t amount_subtract(std::int64_t left, std::int64_t right)
{
    nonnegative(left); nonnegative(right);
    if (left < right) throw std::runtime_error("insufficient amount");
    return left - right;
}

std::int64_t amount_multiply(std::int64_t left, std::int64_t right)
{
    nonnegative(left); nonnegative(right);
    if (right != 0 && left > maximum / right) throw std::overflow_error("amount multiplication overflow");
    return left * right;
}

std::int64_t amount_multiply_divide(std::int64_t left, std::int64_t right,
                                  std::int64_t divisor, bool round_up)
{
    nonnegative(left); nonnegative(right);
    if (divisor <= 0 || divisor > 1000000000) throw std::invalid_argument("invalid amount divisor");
    const auto remainder_product = (left % divisor) * (right % divisor); // < 1e18
    auto result = amount_add(amount_multiply(left / divisor, right),
                             amount_multiply(left % divisor, right / divisor));
    result = amount_add(result, remainder_product / divisor);
    if (round_up && remainder_product % divisor != 0) result = amount_add(result, 1);
    return result;
}

std::int64_t amount_fraction(std::int64_t value, std::int64_t numerator,
                             std::int64_t denominator, bool round_up)
{
    nonnegative(value); nonnegative(numerator);
    if (denominator <= 0 || numerator > denominator) throw std::invalid_argument("invalid amount fraction");
    // Binary long multiplication keeps value * (processed numerator bits) equal to
    // quotient * denominator + remainder with remainder < denominator, so every
    // intermediate fits in 64 unsigned bits without a wider integer type.
    const auto divisor = static_cast<std::uint64_t>(denominator);
    const auto whole = static_cast<std::uint64_t>(value) / divisor;
    const auto part = static_cast<std::uint64_t>(value) % divisor;
    std::uint64_t quotient = 0, remainder = 0;
    for (int bit = 62; bit >= 0; --bit) {
        quotient <<= 1;
        remainder <<= 1;
        if (remainder >= divisor) { remainder -= divisor; ++quotient; }
        if ((static_cast<std::uint64_t>(numerator) >> bit) & 1U) {
            quotient += whole;
            remainder += part;
            if (remainder >= divisor) { remainder -= divisor; ++quotient; }
        }
    }
    if (round_up && remainder != 0) ++quotient;
    return static_cast<std::int64_t>(quotient);
}

std::int64_t signed_add(std::int64_t left, std::int64_t right)
{
    if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right))
        throw std::overflow_error("signed amount overflow");
    return left + right;
}

std::int64_t signed_subtract(std::int64_t left, std::int64_t right)
{
    if ((right < 0 && left > maximum + right) || (right > 0 && left < minimum + right))
        throw std::overflow_error("signed amount overflow");
    return left - right;
}
} // namespace tickforge
