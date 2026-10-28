#pragma once
#include <tickforge/core/orders.hpp>
#include <optional>
#include <string_view>
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

// How a simulated fill came about, from strongest to weakest evidence for resting orders:
// take: our order crossed the spread and consumed visible depth;
// trade_through: a recorded trade printed beyond our resting price;
// book_cross: the opposite side of the recorded book reached our price, no trade print;
// queue: trades at our price used up our estimated queue position.
enum class FillSource { take, trade_through, book_cross, queue };
std::string_view source_name(FillSource source);

// Simulator-only evidence about one fill; a real venue reports none of this.
struct FillDiagnostic {
    std::int64_t fill_id;
    FillSource source;
    // Resting fills only (zero for take): the visible size at our price when the order
    // started resting, all of it ahead of us, and, when the fill happened, our resting
    // quantity before it as a share of the level including us.
    std::int64_t entry_queue_atoms = 0;
    std::int64_t level_share_ppm = 0;
    // Session time since the last market-data gap (a trade ID gap or reorder, or the
    // book losing synchronization); nullopt when none occurred before the fill.
    std::optional<std::int64_t> since_gap_ns = std::nullopt;
};

// A confirmed execution report and when it happened on the session timeline
// (recorded monotonic receive offsets): executed at the venue, then reported to us.
struct ExecutionReport {
    ExecutionResult result;
    std::int64_t executed_ns;
    std::int64_t reported_ns;
    Interruption interruption = Interruption::none;
    std::vector<FillDiagnostic> diagnostics = {}; // one per fill, if the venue knows
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
