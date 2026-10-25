#pragma once
#include <tickforge/core/events.hpp>
#include <tickforge/accounting/account.hpp>
#include <optional>

namespace tickforge::strategy {
// A value snapshot taken after processing the current event, before any decision.
struct View {
    EventTime time;
    bool available;
    std::optional<Level> bid;
    std::optional<Level> ask;
    accounting::Balances balances;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::optional<MarketOrder> on_market(const View& view) = 0;
};
} // namespace tickforge::strategy
