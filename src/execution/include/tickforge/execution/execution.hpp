#pragma once
#include <tickforge/core/orders.hpp>

namespace tickforge::execution {

struct PaperConfig {
    std::int64_t fee_ppm = 1000;
    std::int64_t quantity_step_atoms = 1000;
    std::int64_t max_order_quantity_atoms = 100000000;
    std::int64_t max_position_quantity_atoms = 100000000;
    std::int64_t stale_after_ns = 5000000000;
};

// Initial synchronous, zero-latency contract. Return only confirmed terminal
// results. A thrown/uncertain result must NOT automatically release reservations.
class Execution {
public:
    virtual ~Execution() = default;
    virtual bool available() const = 0;
    virtual ExecutionResult execute(const MarketOrder& order) = 0;
};

void validate_config(const PaperConfig& config);

} // namespace tickforge::execution
