#pragma once
#include <tickforge/core/events.hpp>
#include <tickforge/accounting/account.hpp>
#include <optional>

namespace tickforge::strategy {
// A value snapshot taken after processing the current event, before any decision.
// bid/ask are the best levels this account could still trade against: recorded
// size minus liquidity already consumed by earlier virtual orders.
struct View {
    EventTime time;
    bool available;
    std::optional<Level> bid;
    std::optional<Level> ask;
    accounting::Balances balances;
    accounting::Position position;
    // Account risk state: after a loss limit only sells are accepted; after a halt nothing.
    bool buys_allowed = true;
    bool sells_allowed = true;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::optional<Order> on_market(const View& view) = 0;
};
} // namespace tickforge::strategy
