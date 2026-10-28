#pragma once
#include <tickforge/core/orders.hpp>
#include <vector>

namespace tickforge::execution {

struct PaperConfig {
    std::int64_t fee_ppm = 1000;            // taker: our order crossed the spread
    std::int64_t quantity_step_atoms = 1000;
    std::int64_t max_order_quantity_atoms = 100000000;
    std::int64_t max_position_quantity_atoms = 100000000;
    std::int64_t stale_after_ns = 5000000000;
    // Session-time delays: decision -> execution at the venue, and execution -> the
    // report reaching us. The execution delay decides the price; the report delay
    // decides when balances and the strategy see the result. Cancels travel with the
    // order delay too.
    std::int64_t order_latency_ns = 0;
    std::int64_t report_latency_ns = 0;
    std::int64_t maker_fee_ppm = 1000;      // maker: our resting limit order was hit
};

// Why an accepted order ended without executing against a book.
enum class Interruption { none, no_trusted_book, session_ended };

// A confirmed execution report and when it happened on the session timeline
// (recorded monotonic receive offsets): executed at the venue, then reported to us.
struct ExecutionReport {
    ExecutionResult result;
    std::int64_t executed_ns;
    std::int64_t reported_ns;
    Interruption interruption = Interruption::none;
};

// Asynchronous contract: submit() hands over an order; its reports arrive later through
// take_reports(), in order: zero or more open reports (a resting limit order, with any
// new fills), then exactly one terminal report. cancel() asks to cancel a resting
// order; it may arrive too late, after the order filled. A throw from any call means
// the outcome is uncertain: reservations must NOT be released automatically.
class Execution {
public:
    virtual ~Execution() = default;
    virtual bool available() const = 0;
    virtual void submit(const Order& order) = 0;
    virtual void cancel(std::int64_t order_id) = 0;
    virtual std::vector<ExecutionReport> take_reports() = 0;
};

void validate_config(const PaperConfig& config);

} // namespace tickforge::execution
