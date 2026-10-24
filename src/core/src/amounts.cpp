#include <tickforge/core/amounts.hpp>
#include <limits>
#include <stdexcept>

namespace tickforge {
namespace {
constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
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
} // namespace tickforge
