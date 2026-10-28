#include "paper_reporter.hpp"
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/console/console.hpp>
#include <format>

namespace tickforge::app {
namespace {
std::string money(std::int64_t atoms)
{
    return std::format("{}.{:08}", atoms / 100000000, atoms % 100000000);
}
std::string signed_money(std::int64_t atoms)
{
    // Magnitude in unsigned arithmetic so that INT64_MIN cannot overflow.
    const auto magnitude = atoms < 0 ? 0 - static_cast<std::uint64_t>(atoms) : static_cast<std::uint64_t>(atoms);
    return std::format("{}{}.{:08}", atoms < 0 ? "-" : "+", magnitude / 100000000, magnitude % 100000000);
}
void position(const accounting::Balances& balance, const accounting::Position& held)
{
    const auto average = accounting::average_cost_price(balance, held);
    write_console(std::format("  PNL     | cost basis={} USDT | average cost={} | realized={} USDT\n",
        money(held.base_cost_quote), average ? price_text(*average) + " USDT" : std::string("unavailable (no BTC)"),
        signed_money(held.realized_pnl_quote)));
}
std::string_view status(OrderStatus value)
{
    switch (value) {
    case OrderStatus::filled: return "FILLED";
    case OrderStatus::partially_filled: return "PARTIAL (remainder cancelled)";
    case OrderStatus::cancelled: return "CANCELLED";
    case OrderStatus::open: return "OPEN (resting in the book)";
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

std::string money_text(std::int64_t atoms) { return money(atoms); }

namespace {
std::string risk_text(const execution::RiskLimits& limits)
{
    const auto limit = [](std::int64_t value, std::string_view unit) {
        return value == 0 ? std::string("off") : std::format("{} {}", value, unit);
    };
    return std::format("max loss={} | max drawdown={} | max orders/minute={} (losses: sells only; rate: halt)",
        limits.max_loss_quote == 0 ? std::string("off") : money(limits.max_loss_quote) + " USDT",
        limits.max_drawdown_quote == 0 ? std::string("off") : money(limits.max_drawdown_quote) + " USDT",
        limit(limits.max_orders_per_minute, "requests"));
}
} // namespace

std::string overrides_text(const std::vector<std::string>& overrides)
{
    if (overrides.empty()) return "none";
    std::string text;
    for (const auto& assignment : overrides) text += (text.empty() ? "" : "; ") + assignment;
    return text;
}

void paper_header(std::string_view session, std::string_view scenario, const PaperScenario& settings)
{
    write_console(std::format(
        "tickforge | Paper trading (offline, virtual funds only)\n"
        "Market    | {}\nConfig    | {}\nOverrides | {}\n"
        "Execution | market orders, unfilled remainder cancelled\n"
        "Latency   | order={} ns (decision -> execution) | report={} ns (execution -> confirmed result)\n"
        "Fee       | taker {} ppm | maker {} ppm, in USDT (configured assumptions, not a live venue tier)\n"
        "Limits    | order={} BTC | position={} BTC | quantity step={} BTC | stale after={} ns\n"
        "Liquidity | consumed virtually; only explicit level updates or snapshots replenish it\n"
        "Risk      | {}\n"
        "Initial   | {} USDT | {} BTC | BTC cost basis={} USDT\n"
        "PnL       | average cost; buy fees in cost, sell fees in proceeds; marked at last trusted bid\n\n",
        session, scenario, overrides_text(settings.overrides), settings.config.order_latency_ns, settings.config.report_latency_ns, settings.config.fee_ppm, settings.config.maker_fee_ppm,
        quantity_text(settings.config.max_order_quantity_atoms), quantity_text(settings.config.max_position_quantity_atoms),
        quantity_text(settings.config.quantity_step_atoms), settings.config.stale_after_ns, risk_text(settings.risk),
        money(settings.initial_quote_atoms), quantity_text(settings.initial_base_atoms),
        money(settings.initial_base_cost_atoms)));
}

void paper_market(const MarketEvent& event, bool available)
{
    write_console(std::format("Market #{} | elapsed={} ns | execution {}\n",
        event.time.ordinal, event.time.receive_elapsed_ns, available ? "AVAILABLE" : "BLOCKED (unavailable/stale book)"));
}

void paper_order(const execution::OrderReport& report, bool first)
{
    const auto& order = report.order;
    const auto limit = order.type == OrderType::limit
        ? std::format(" | LIMIT {} USDT{}", price_text(order.limit_price_atoms), order.post_only ? " post-only" : "")
        : std::string();
    write_console(std::format("Order #{} | {} {} BTC{} | max spend={} USDT\n",
        order.id, order.side == Side::buy ? "BUY" : "SELL", quantity_text(order.quantity_atoms), limit,
        money(order.max_spend_quote_atoms)));
    if (report.accepted && first) {
        write_console("  ACCEPTED | funds reserved before execution\n");
        balances(report.reserved_balances);
    }
    for (const auto& fill : report.result.fills) {
        // Maker fills name the evidence: queue, trade_through or book_cross.
        std::string maker;
        if (fill.liquidity == Liquidity::maker) {
            maker = " | maker";
            for (const auto& note : report.diagnostics)
                if (note.fill_id == fill.id) maker += std::format(" via {}", execution::source_name(note.source));
        }
        write_console(std::format("  FILL #{} | price={} USDT | quantity={} BTC | notional={} USDT | fee={} USDT{}\n",
            fill.id, price_text(fill.price_atoms), quantity_text(fill.quantity_atoms), money(fill.quote_atoms),
            money(fill.fee_quote_atoms), maker));
    }
    if (!report.final) {
        // A resting order: fills so far, nothing released yet.
        write_console(std::format("  RESULT  | {} | remaining={} BTC | {}\n", status(report.result.status),
            quantity_text(report.result.remaining_quantity_atoms), report.result.reason));
        if (!report.result.fills.empty()) {
            write_console("  ACCOUNT | after confirmed fills; the reservation stays until the order ends\n");
            balances(report.final_balances);
            position(report.final_balances, report.final_position);
        }
        write_console("\n");
        return;
    }
    write_console(std::format("  RESULT  | {} | remaining={} BTC | {}\n", status(report.result.status),
        quantity_text(report.result.remaining_quantity_atoms), report.result.reason));
    write_console("  ACCOUNT | after confirmed fills and release of unused reserves\n");
    balances(report.final_balances);
    position(report.final_balances, report.final_position);
    write_console("\n");
}

void paper_summary(const accounting::Account& account, std::size_t orders, std::size_t rejected,
                   std::size_t fills, std::int64_t fees, std::optional<Mark> mark)
{
    write_console(std::format("Paper complete | {} requests | {} rejected | {} fills | fees={} USDT\n",
        orders, rejected, fills, money(fees)));
    balances(account.balances());
    position(account.balances(), account.position());
    const auto realized = signed_money(account.position().realized_pnl_quote);
    if (mark) {
        const auto value = accounting::value_at(account, mark->price_atoms);
        write_console(std::format(
            "  MARK    | bid {} USDT at event #{} (before exit fees and depth)\n"
            "  VALUE   | BTC={} USDT | equity={} USDT\n"
            "  TOTAL   | realized={} USDT | unrealized={} USDT | total={} USDT\n",
            price_text(mark->price_atoms), mark->ordinal, money(value.market_value_quote), money(value.equity_quote),
            realized, signed_money(value.unrealized_pnl_quote), signed_money(value.total_pnl_quote)));
    } else if (account.balances().base_total == 0) {
        write_console(std::format("  TOTAL   | realized={} USDT | unrealized=+0.00000000 USDT | total={} USDT\n",
            realized, realized));
    } else {
        write_console(std::format("  TOTAL   | realized={} USDT | unrealized=unavailable (no trusted bid observed)\n",
            realized));
    }
}
} // namespace tickforge::app

namespace tickforge::app {
void paper_fill_diagnostics(const FillStats& stats, const Markouts& markouts)
{
    const auto count = [&](execution::FillSource source) { return stats.by_source[static_cast<std::size_t>(source)]; };
    write_console(std::format(
        "  FILLS   | take {} | trade_through {} | book_cross {} | queue {} | within 1 s of a data gap {} | "
        "max level share {}.{:04}%\n",
        count(execution::FillSource::take), count(execution::FillSource::trade_through),
        count(execution::FillSource::book_cross), count(execution::FillSource::queue), stats.near_gap,
        stats.max_level_share_ppm / 10000, stats.max_level_share_ppm % 10000));
    for (const auto liquidity : {Liquidity::maker, Liquidity::taker}) {
        std::string line = std::format("  MARKOUT | {} |", liquidity == Liquidity::maker ? "maker" : "taker");
        for (std::size_t h = 0; h < markout_horizons_ns.size(); ++h) {
            const auto& total = markouts.total(liquidity, h);
            // The per-BTC average is a display ratio only; the CSV keeps exact atoms.
            line += std::format(" {}s {} USDT over {} fills{} |", markout_horizons_ns[h] / 1000000000,
                signed_money(total.quote_atoms), total.fills,
                total.quantity_atoms == 0 ? std::string()
                    : std::format(" ({:+.2f} USDT/BTC)", static_cast<double>(total.quote_atoms) / total.quantity_atoms));
        }
        line.resize(line.size() - 2);
        write_console(line + "\n");
    }
}
} // namespace tickforge::app
