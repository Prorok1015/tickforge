#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/execution/risk.hpp>
#include <tickforge/accounting/account.hpp>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace tickforge::execution {

struct OrderReport {
    Order order;              // as accepted: a limit buy carries its derived reservation
    bool accepted;
    ExecutionResult result;   // fills: new in this report only
    accounting::Balances reserved_balances;
    accounting::Balances final_balances;
    accounting::Position final_position;
    std::int64_t executed_ns = 0; // session time; zero for requests rejected before execution
    std::int64_t reported_ns = 0;
    Interruption interruption = Interruption::none;
    bool final = true;                    // false: the order still rests and more reports follow
    std::int64_t filled_quantity_atoms = 0; // cumulative for this order
    std::int64_t fee_quote_atoms = 0;       // cumulative for this order
};

struct OpenOrder {
    Order order;
    std::int64_t filled_quantity_atoms;
    bool cancel_requested;
};

// Lifecycle: submit() validates, reserves, and hands the order to execution. Confirmed
// reports are committed to the account when collect() receives them: fills as they
// come, the reservation released only by the terminal report.
class OrderManager {
public:
    // An optional risk guard is consulted for every request before other checks.
    OrderManager(accounting::Account& account, Execution& execution, PaperConfig config, RiskGuard* risk = nullptr);
    // Returns the final report of a request rejected before execution; nullopt once
    // accepted, in which case its reports come from later collect() calls.
    std::optional<OrderReport> submit(const Order& order);
    // Requests a cancel of an accepted, unfinished order; false if there is none.
    bool cancel(std::int64_t order_id);
    // Cancels every unfinished limit order, or those on one side; returns how many.
    std::size_t cancel_open(std::optional<Side> side = std::nullopt);
    // Commits every report execution has delivered so far, in report order.
    std::vector<OrderReport> collect();
    std::size_t in_flight() const { return pending_.size(); }
    std::vector<OpenOrder> open_orders() const;
private:
    OrderReport commit(ExecutionReport report);
    std::int64_t limit_buy_reservation(const Order& order) const;
    struct Pending {
        Order order;
        accounting::Balances reserved;
        std::int64_t filled = 0;
        std::int64_t fees = 0;
        bool cancel_requested = false;
    };
    accounting::Account& account_;
    Execution& execution_;
    PaperConfig config_;
    RiskGuard* risk_;
    std::set<std::int64_t> submitted_;
    std::map<std::int64_t, Pending> pending_;
};

} // namespace tickforge::execution
