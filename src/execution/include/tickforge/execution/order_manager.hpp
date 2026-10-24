#pragma once
#include <tickforge/execution/execution.hpp>
#include <tickforge/accounting/account.hpp>
#include <set>

namespace tickforge::execution {

struct OrderReport {
    MarketOrder order;
    bool accepted;
    ExecutionResult result;
    accounting::Balances reserved_balances;
    accounting::Balances final_balances;
};

class OrderManager {
public:
    OrderManager(accounting::Account& account, Execution& execution, PaperConfig config);
    OrderReport submit(const MarketOrder& order);
private:
    accounting::Account& account_;
    Execution& execution_;
    PaperConfig config_;
    std::set<std::int64_t> submitted_;
};

} // namespace tickforge::execution
