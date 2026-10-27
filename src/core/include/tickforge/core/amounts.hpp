#pragma once
#include <cstdint>

namespace tickforge {
// Checked, nonnegative integer arithmetic. No compiler-specific wide integers.
std::int64_t amount_add(std::int64_t left, std::int64_t right);
std::int64_t amount_subtract(std::int64_t left, std::int64_t right);
std::int64_t amount_multiply(std::int64_t left, std::int64_t right);
// divisor must be 1..1e9; avoids overflowing intermediate left * right.
std::int64_t amount_multiply_divide(std::int64_t left, std::int64_t right,
                                  std::int64_t divisor, bool round_up);
// value * numerator / denominator for 0 <= numerator <= denominator; any positive
// denominator. The result never exceeds value, so it cannot overflow.
std::int64_t amount_fraction(std::int64_t value, std::int64_t numerator,
                             std::int64_t denominator, bool round_up);

// Checked signed arithmetic for profit and loss, which may be negative.
std::int64_t signed_add(std::int64_t left, std::int64_t right);
std::int64_t signed_subtract(std::int64_t left, std::int64_t right);
} // namespace tickforge
