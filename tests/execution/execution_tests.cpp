#include <tickforge/execution/order_manager.hpp>
#include <tickforge/execution/simulator.hpp>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <utility>

namespace {
using namespace tickforge;
using namespace tickforge::execution;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "expected exception");
}
template<class Data> MarketEvent event(std::int64_t ordinal, Data data, std::int64_t elapsed = 0)
{
    return {{ordinal, 0, 0, elapsed}, std::move(data)};
}
PaperConfig config() { return {1000, 1, 1000000000, 1000000000, 100}; }

void routing_and_liquidity()
{
    auto settings = config();
    Simulator simulator(settings);
    accounting::Account account(1000000000000, 0, 0); // 10000 USDT
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{100, {{9900, 200000000}, {10000, 100000000}},
        {{10200, 200000000}, {10100, 100000000}}}));
    auto result = manager.submit({1, Side::buy, 250000000, 30000000000});
    check(result.result.status == OrderStatus::filled && result.result.fills.size() == 2,
          "buy must walk asks across levels");
    check(result.result.fills[0].price_atoms == 10100 && result.result.fills[0].quantity_atoms == 100000000 &&
          result.result.fills[1].price_atoms == 10200 && result.result.fills[1].quantity_atoms == 150000000,
          "asks must be consumed cheapest first");
    check(account.balances().quote_total == 974574600000 && account.balances().base_total == 250000000 &&
          account.balances().quote_reserved == 0, "buy costs, fees, BTC and released budget must reconcile");
    result = manager.submit({2, Side::buy, 100000000, 20000000000});
    check(result.result.status == OrderStatus::partially_filled && result.result.remaining_quantity_atoms == 50000000,
          "second order must see previously consumed liquidity");
    check(account.balances().quote_total == 969469500000, "partial fill cost must be exact");
    auto before = account.balances();
    result = manager.submit({3, Side::buy, 10000000, 2000000000});
    check(result.result.status == OrderStatus::cancelled && result.result.fills.empty() && account.balances() == before,
          "exhausted asks must not refill per request");
    simulator.on_market_event(event(2, Trade{1, 100000000}));
    check(manager.submit({4, Side::buy, 10000000, 2000000000}).result.fills.empty(), "trades must not replenish book depth");
    simulator.on_market_event(event(3, BookUpdate{101, 101, {{10000, 100000000}}, {}}));
    check(manager.submit({5, Side::buy, 10000000, 2000000000}).result.fills.empty(), "bid updates must not refill asks");
    simulator.on_market_event(event(4, BookUpdate{102, 102, {}, {{10200, 100000000}}}));
    result = manager.submit({6, Side::buy, 100000000, 20000000000});
    check(result.result.status == OrderStatus::filled && result.result.fills[0].price_atoms == 10200,
          "an explicit ask update replenishes only that level");
    result = manager.submit({7, Side::sell, 150000000, 0});
    check(result.result.status == OrderStatus::filled && result.result.fills.size() == 2 &&
          result.result.fills[0].price_atoms == 10000 && result.result.fills[1].price_atoms == 9900,
          "sell must walk bids highest first");
    check(account.balances().base_total == 250000000 && account.balances().quote_total == 974194350000 &&
          account.balances().base_reserved == 0, "sell cash and commission must reconcile");
}

void budget_and_rounding()
{
    auto settings = config(); settings.quantity_step_atoms = 1000;
    Simulator simulator(settings);
    accounting::Account account(100000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{10000, 100000000}}, {{10100, 100000000}}}));
    const auto result = manager.submit({1, Side::buy, 2000, 101101});
    check(result.result.status == OrderStatus::partially_filled && result.result.fills[0].quantity_atoms == 1000 &&
          result.result.fills[0].quote_atoms == 101000 && result.result.fills[0].fee_quote_atoms == 101,
          "buy budget must include fees and respect quantity step");
    check(account.balances().quote_total == 99898899 && account.balances().quote_reserved == 0, "budget accounting");
    const auto before = account.balances();
    check(manager.submit({2, Side::buy, 1000, 1}).result.status == OrderStatus::cancelled &&
          account.balances() == before, "unaffordable lot must not create a fill or retain a reservation");

    settings.quantity_step_atoms = 1;
    Simulator tiny(settings);
    tiny.on_market_event(event(1, BookSnapshot{1, {{1, 100}}, {{2, 100}}}));
    auto rounded = tiny.execute({1, Side::buy, 1, 2});
    check(rounded.fills.size() == 1 && rounded.fills[0].quote_atoms == 1 && rounded.fills[0].fee_quote_atoms == 1,
          "buy notional and fee round up at the smallest money unit");
    rounded = tiny.execute({2, Side::sell, 1, 0});
    check(rounded.fills.size() == 1 && rounded.fills[0].quote_atoms == 0 && rounded.fills[0].fee_quote_atoms == 0,
          "sell proceeds round down");
}

void risk_and_freshness()
{
    auto settings = config(); settings.quantity_step_atoms = 1000;
    settings.max_order_quantity_atoms = 100000; settings.max_position_quantity_atoms = 150000;
    Simulator simulator(settings);
    accounting::Account account(10000000000, 0, 0);
    OrderManager manager(account, simulator, settings);
    check(!manager.submit({1, Side::buy, 100000, 100000000}).accepted, "no snapshot must block orders");
    simulator.on_market_event(event(1, BookSnapshot{100, {{10000, 100000000}}, {{10100, 100000000}}}));
    const auto original = account.balances();
    check(!manager.submit({2, Side::sell, 100000, 0}).accepted, "no short selling");
    check(!manager.submit({3, Side::buy, 101000, 100000000}).accepted, "order size risk");
    check(!manager.submit({4, Side::buy, 999, 100000000}).accepted, "quantity step risk");
    check(!manager.submit({5, Side::buy, 100000, 10000000001}).accepted, "insufficient USDT");
    check(account.balances() == original, "risk rejection must not reserve or spend");
    check(manager.submit({6, Side::buy, 100000, 100000000}).accepted, "valid order accepted");
    const auto before = account.balances();
    check(!manager.submit({7, Side::buy, 100000, 100000000}).accepted, "position limit uses requested maximum quantity");
    check(!manager.submit({6, Side::buy, 100000, 100000000}).accepted && account.balances() == before,
          "duplicate order ID must not execute twice");
    simulator.on_market_event(event(2, Trade{10100, 1}, 101));
    check(!simulator.available() && !manager.submit({8, Side::sell, 100000, 0}).accepted, "stale depth must block decisions");
    simulator.on_market_event(event(3, BookUpdate{99, 100, {}, {}}, 102));
    check(!simulator.available(), "stale duplicate range must not refresh time");
    simulator.on_market_event(event(4, BookUpdate{101, 101, {}, {}}, 103));
    check(simulator.available(), "valid next range refreshes depth time");
    simulator.on_market_event(event(5, BookUpdate{103, 103, {}, {}}, 104));
    check(!simulator.available() && !manager.submit({9, Side::sell, 100000, 0}).accepted, "gap blocks orders");
    simulator.on_market_event(event(6, BookSnapshot{200, {{10000, 100000}}, {{10100, 100000}}, true}, 105));
    check(!simulator.available(), "live snapshot must still wait for a bridge");
    simulator.on_market_event(event(7, BookUpdate{201, 201, {}, {}}, 106));
    check(simulator.available(), "bridging range restores execution");
    simulator.on_market_event(event(8, BookReset{"disconnect"}, 107));
    check(!simulator.available(), "disconnect clears execution availability");
    check(account.balances() == before, "blocked orders do not alter account");
}

void partial_sell()
{
    const auto settings = config();
    Simulator simulator(settings);
    accounting::Account account(0, 200000000, 20000000000); // 2 BTC bought at 100.00
    OrderManager manager(account, simulator, settings);
    simulator.on_market_event(event(1, BookSnapshot{1, {{10000, 100000000}}, {{10100, 100000000}}}));
    const auto result = manager.submit({1, Side::sell, 150000000, 0});
    check(result.result.status == OrderStatus::partially_filled && result.result.remaining_quantity_atoms == 50000000,
          "sell remainder must be cancelled when known bids are exhausted");
    check(account.balances().base_total == 100000000 && account.balances().base_reserved == 0 &&
          account.balances().available_base() == 100000000 && account.balances().quote_total == 9990000000,
          "partial sell must free unused BTC and charge fees only on actual fills");
    check(account.position() == accounting::Position{10000000000, -10000000},
          "selling at cost loses exactly the fee; half the cost remains");
    simulator.on_market_event(event(2, BookSnapshot{2, {{10000, 100000000}}, {{10100, 100000000}}}));
    check(manager.submit({2, Side::sell, 100000000, 0}).result.status == OrderStatus::filled &&
          account.balances().base_total == 0, "new snapshot replenishes known liquidity");
    check(account.position() == accounting::Position{0, -20000000}, "closing the position removes all cost");
    simulator.on_market_event(event(3, BookSnapshot{3, {}, {}}));
    const auto before = account.balances();
    check(manager.submit({3, Side::buy, 1, 1}).result.status == OrderStatus::cancelled && account.balances() == before,
          "empty book must not invent an execution price");
}

class UncertainExecution final : public Execution {
public:
    bool available() const override { return true; }
    ExecutionResult execute(const MarketOrder&) override { throw std::runtime_error("result unknown"); }
};
class MalformedExecution final : public Execution {
public:
    bool available() const override { return true; }
    ExecutionResult execute(const MarketOrder& order) override
    { return {order.id, OrderStatus::filled, {}, 0, "invalid empty filled report"}; }
};

class DuplicateExecution final : public Execution {
public:
    bool available() const override { return true; }
    ExecutionResult execute(const MarketOrder& order) override
    {
        const Fill fill{1, order.id, order.side, 100, 1, 1, 0};
        return {order.id, OrderStatus::filled, {fill, fill}, 0, "duplicate confirmed receipt"};
    }
};
class RejectedExecution final : public Execution {
public:
    bool available() const override { return true; }
    ExecutionResult execute(const MarketOrder& order) override
    { return {order.id, OrderStatus::rejected, {}, order.quantity_atoms, "confirmed rejection"}; }
};

void uncertain_results()
{
    const auto settings = config();
    accounting::Account account(100, 0, 0);
    UncertainExecution execution;
    OrderManager manager(account, execution, settings);
    rejects([&] { manager.submit({1, Side::buy, 1, 10}); });
    check(account.balances().quote_reserved == 10 && account.balances().quote_total == 100,
          "unknown result must retain its reservation");
    MalformedExecution malformed;
    OrderManager other(account, malformed, settings);
    rejects([&] { other.submit({2, Side::buy, 1, 10}); });
    check(account.balances().quote_reserved == 20 && account.balances().base_total == 0,
          "malformed result must not silently release reserves or invent BTC");
    accounting::Account other_account(100, 0, 0);
    DuplicateExecution duplicate;
    OrderManager duplicate_manager(other_account, duplicate, settings);
    const auto report = duplicate_manager.submit({1, Side::buy, 1, 10});
    check(report.result.fills.size() == 1 && other_account.balances().base_total == 1 &&
          other_account.balances().quote_total == 99, "duplicate receipts must not duplicate journal fills or money");
    RejectedExecution rejected;
    OrderManager rejected_manager(other_account, rejected, settings);
    const auto before = other_account.balances();
    check(rejected_manager.submit({2, Side::buy, 1, 10}).result.status == OrderStatus::rejected &&
          other_account.balances() == before, "confirmed rejection releases the whole reservation");
}
}

int main()
{
    try {
        routing_and_liquidity(); budget_and_rounding(); risk_and_freshness(); partial_sell(); uncertain_results();
        std::fputs("Execution, risk, and liquidity checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Execution check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
