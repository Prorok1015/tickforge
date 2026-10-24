#include "paper_reporter.hpp"
#include <tickforge/console/console.hpp>
#include <format>

namespace tickforge::app {
namespace {
std::string money(std::int64_t atoms)
{
    return std::format("{}.{:08}", atoms / 100000000, atoms % 100000000);
}
std::string_view status(OrderStatus value)
{
    switch (value) {
    case OrderStatus::filled: return "FILLED";
    case OrderStatus::partially_filled: return "PARTIAL (remainder cancelled)";
    case OrderStatus::cancelled: return "CANCELLED";
    case OrderStatus::rejected: return "REJECTED";
    }
    return "UNKNOWN";
}
void balances(const accounting::Balances& balance)
{
    write_console(std::format(
        "  USDT    | total={} | reserved={} | available={}\n"
        "  BTC     | total={} | reserved={} | available={}\n",
        money(balance.quote_total), money(balance.quote_reserved), money(balance.available_quote()),
        quantity_text(balance.base_total), quantity_text(balance.base_reserved), quantity_text(balance.available_base())));
}
}

void paper_header(std::string_view session, std::string_view scenario, const PaperScenario& settings)
{
    write_console(std::format(
        "tickforge | Paper trading (offline, virtual funds only)\n"
        "Market    | {}\nScenario  | {}\n"
        "Execution | market orders, zero latency, unfilled remainder cancelled\n"
        "Fee       | {} ppm in USDT (configured assumption, not a live venue fee)\n"
        "Limits    | order={} BTC | position={} BTC | quantity step={} BTC | stale after={} ns\n"
        "Liquidity | consumed virtually; only explicit level updates or snapshots replenish it\n"
        "Initial   | {} USDT | {} BTC\n\n",
        session, scenario, settings.config.fee_ppm,
        quantity_text(settings.config.max_order_quantity_atoms), quantity_text(settings.config.max_position_quantity_atoms),
        quantity_text(settings.config.quantity_step_atoms), settings.config.stale_after_ns,
        money(settings.initial_quote_atoms), quantity_text(settings.initial_base_atoms)));
}

void paper_market(const MarketEvent& event, bool available)
{
    write_console(std::format("Market #{} | elapsed={} ns | execution {}\n",
        event.time.ordinal, event.time.receive_elapsed_ns, available ? "AVAILABLE" : "BLOCKED (unavailable/stale book)"));
}

void paper_order(const execution::OrderReport& report)
{
    write_console(std::format("Order #{} | {} {} BTC | max spend={} USDT\n",
        report.order.id, report.order.side == Side::buy ? "BUY" : "SELL",
        quantity_text(report.order.quantity_atoms), money(report.order.max_spend_quote_atoms)));
    if (report.accepted) {
        write_console("  ACCEPTED | funds reserved before execution\n");
        balances(report.reserved_balances);
    }
    for (const auto& fill : report.result.fills) {
        write_console(std::format("  FILL #{} | price={} USDT | quantity={} BTC | notional={} USDT | fee={} USDT\n",
            fill.id, price_text(fill.price_atoms), quantity_text(fill.quantity_atoms), money(fill.quote_atoms), money(fill.fee_quote_atoms)));
    }
    write_console(std::format("  RESULT  | {} | remaining={} BTC | {}\n", status(report.result.status),
        quantity_text(report.result.remaining_quantity_atoms), report.result.reason));
    write_console("  ACCOUNT | after confirmed fills and release of unused reserves\n");
    balances(report.final_balances);
    write_console("\n");
}

void paper_summary(const accounting::Account& account, std::size_t orders, std::size_t rejected,
                   std::size_t fills, std::int64_t fees)
{
    write_console(std::format("Paper complete | {} requests | {} rejected | {} fills | fees={} USDT\n",
        orders, rejected, fills, money(fees)));
    balances(account.balances());
}
} // namespace tickforge::app
