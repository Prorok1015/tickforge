#pragma once
#include <tickforge/core/orders.hpp>
#include <vector>

namespace tickforge::execution {

struct PaperConfig {
    std::int64_t fee_ppm = 1000;
    std::int64_t quantity_step_atoms = 1000;
    std::int64_t max_order_quantity_atoms = 100000000;
    std::int64_t max_position_quantity_atoms = 100000000;
    std::int64_t stale_after_ns = 5000000000;
    // Session-time delays: decision -> execution at the venue, and execution -> the
    // report reaching us. The execution delay decides the price; the report delay
    // decides when balances and the strategy see the result.
    std::int64_t order_latency_ns = 0;
    std::int64_t report_latency_ns = 0;
};

// Why an accepted order ended without executing against a book.
enum class Interruption { none, no_trusted_book, session_ended };

// A confirmed terminal result and when it happened on the session timeline
// (recorded monotonic receive offsets): executed at the venue, then reported to us.
struct ExecutionReport {
    ExecutionResult result;
    std::int64_t executed_ns;
    std::int64_t reported_ns;
    Interruption interruption = Interruption::none;
};

// Asynchronous contract: submit() hands over an order; its confirmed terminal result
// arrives later, exactly once, through take_reports(). A throw from either call means
// the outcome is uncertain: reservations must NOT be released automatically.
class Execution {
public:
    virtual ~Execution() = default;
    virtual bool available() const = 0;
    virtual void submit(const MarketOrder& order) = 0;
    virtual std::vector<ExecutionReport> take_reports() = 0;
};

void validate_config(const PaperConfig& config);

} // namespace tickforge::execution
