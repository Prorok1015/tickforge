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
    MarketOrder order;
    bool accepted;
    ExecutionResult result;
    accounting::Balances reserved_balances;
    accounting::Balances final_balances;
    accounting::Position final_position;
    std::int64_t executed_ns = 0; // session time; zero for requests rejected before execution
    std::int64_t reported_ns = 0;
    Interruption interruption = Interruption::none;
};

// Lifecycle: submit() validates, reserves, and hands the order to execution. The
// confirmed result is committed to the account later, when collect() receives it.
class OrderManager {
public:
    // An optional risk guard is consulted for every request before other checks.
    OrderManager(accounting::Account& account, Execution& execution, PaperConfig config, RiskGuard* risk = nullptr);
    // Returns the final report of a request rejected before execution; nullopt once
    // accepted, in which case its report comes from a later collect().
    std::optional<OrderReport> submit(const MarketOrder& order);
    // Commits every result execution has reported so far, in report order.
    std::vector<OrderReport> collect();
    std::size_t in_flight() const { return pending_.size(); }
private:
    OrderReport commit(ExecutionReport report);
    struct Pending { MarketOrder order; accounting::Balances reserved; };
    accounting::Account& account_;
    Execution& execution_;
    PaperConfig config_;
    RiskGuard* risk_;
    std::set<std::int64_t> submitted_;
    std::map<std::int64_t, Pending> pending_;
};

} // namespace tickforge::execution
