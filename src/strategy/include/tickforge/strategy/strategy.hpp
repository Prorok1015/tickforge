#pragma once
#include <tickforge/core/events.hpp>
#include <tickforge/core/orders.hpp>
#include <tickforge/accounting/account.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tickforge::strategy {

// An accepted order of this account that has not ended yet: in flight or resting.
struct OpenOrder {
    Order order;
    std::int64_t filled_quantity_atoms;
    bool cancel_requested;
};

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
    std::vector<OpenOrder> open_orders = {};
};

enum class Signal { none, buy, take_profit, stop_loss };

// What a strategy may ask for. It never executes anything itself: places go through
// the risk guard and order manager, cancels may arrive too late, and notices only
// explain a decision in the journal.
struct Place { Order order; Signal signal; };
struct Cancel { std::int64_t order_id; };
struct Notice { std::string text; };
using Action = std::variant<Place, Cancel, Notice>;

class Strategy {
public:
    virtual ~Strategy() = default;
    // Called once per event; actions are carried out in the returned order.
    virtual std::vector<Action> on_market(const View& view) = 0;
};
} // namespace tickforge::strategy
