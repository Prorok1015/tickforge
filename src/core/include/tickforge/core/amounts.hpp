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
} // namespace tickforge
