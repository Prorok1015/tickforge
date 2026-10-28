#include "paper_export.hpp"
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/core/amounts.hpp>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tickforge::app {
namespace {
// RFC 4180 quoting only when needed; paths and reasons may contain commas.
std::string csv(std::string_view text)
{
    if (text.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(text);
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + '"';
}

std::string_view status_name(OrderStatus status)
{
    switch (status) {
    case OrderStatus::rejected: return "rejected";
    case OrderStatus::filled: return "filled";
    case OrderStatus::partially_filled: return "partially_filled";
    case OrderStatus::cancelled: return "cancelled";
    }
    return "unknown";
}

std::string_view side_name(Side side) { return side == Side::buy ? "buy" : "sell"; }

std::string_view interruption_name(execution::Interruption interruption)
{
    switch (interruption) {
    case execution::Interruption::none: return "none";
    case execution::Interruption::no_trusted_book: return "no_trusted_book";
    case execution::Interruption::session_ended: return "session_ended";
    }
    return "unknown";
}

void write(std::FILE* file, std::string_view text)
{
    if (std::fwrite(text.data(), 1, text.size(), file) != text.size())
        throw std::runtime_error("report write failed");
}
} // namespace

PaperExport::PaperExport(std::string_view prefix)
    : orders_path_(std::format("{}-orders.csv", prefix)), fills_path_(std::format("{}-fills.csv", prefix)),
      summary_path_(std::format("{}-summary.csv", prefix))
{
    for (const auto* path : {&orders_path_, &fills_path_, &summary_path_})
        if (std::filesystem::exists(*path)) throw std::runtime_error(std::format("report '{}' already exists", *path));
    const auto create = [](const std::string& path) {
        File file(std::fopen(path.c_str(), "wx"));
        if (!file) throw std::runtime_error(std::format("cannot create new report '{}': {}", path, std::strerror(errno)));
        return file;
    };
    orders_ = create(orders_path_);
    fills_ = create(fills_path_);
    // Times are recorded session offsets: submission, execution at the venue, report to us.
    write(orders_.get(), "submitted_event_ordinal,submitted_ns,order_id,signal,side,quantity_atoms,max_spend_quote_atoms,"
        "accepted,status,filled_quantity_atoms,remaining_quantity_atoms,fill_count,fee_quote_atoms,"
        "quote_total_atoms,base_total_atoms,cost_basis_quote_atoms,realized_pnl_quote_atoms,"
        "executed_ns,reported_ns,interruption,reason\n");
    write(fills_.get(), "submitted_event_ordinal,submitted_ns,order_id,fill_id,side,price_atoms,quantity_atoms,"
        "quote_atoms,fee_quote_atoms,executed_ns\n");
}

void PaperExport::order(const EventTime& submitted, std::string_view signal, const execution::OrderReport& report)
{
    const auto& result = report.result;
    std::int64_t fees = 0;
    for (const auto& fill : result.fills) {
        fees = amount_add(fees, fill.fee_quote_atoms);
        write(fills_.get(), std::format("{},{},{},{},{},{},{},{},{},{}\n", submitted.ordinal, submitted.receive_elapsed_ns,
            fill.order_id, fill.id, side_name(fill.side), fill.price_atoms, fill.quantity_atoms, fill.quote_atoms,
            fill.fee_quote_atoms, report.executed_ns));
    }
    const auto& balances = report.final_balances;
    // Execution and report times are empty for requests rejected before execution.
    const auto time = [&](std::int64_t value) { return report.accepted ? std::format("{}", value) : std::string(); };
    write(orders_.get(), std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
        submitted.ordinal, submitted.receive_elapsed_ns, report.order.id, signal, side_name(report.order.side),
        report.order.quantity_atoms, report.order.max_spend_quote_atoms, report.accepted ? 1 : 0,
        status_name(result.status), report.order.quantity_atoms - result.remaining_quantity_atoms,
        result.remaining_quantity_atoms, result.fills.size(), fees, balances.quote_total, balances.base_total,
        report.final_position.base_cost_quote, report.final_position.realized_pnl_quote,
        time(report.executed_ns), time(report.reported_ns), interruption_name(report.interruption), csv(result.reason)));
}

void PaperExport::complete(std::string_view command, std::string_view session, std::string_view scenario_path,
                           const PaperScenario& scenario, const accounting::Account& account,
                           const PaperTotals& totals, std::optional<Mark> mark)
{
    // The journals must be on disk before the summary claims completion.
    for (auto* file : {&orders_, &fills_})
        if (std::fclose(file->release()) != 0) throw std::runtime_error("report close failed");

    std::vector<std::pair<std::string, std::string>> rows;
    const auto add = [&](std::string key, auto value) { rows.emplace_back(std::move(key), std::format("{}", value)); };
    add("format", "tickforge-paper-summary");
    add("version", 2);
    add("command", command);
    add("session", csv(session));
    add("scenario", csv(scenario_path));
    add("instrument", "BTCUSDT");
    add("price_scale", 100);
    add("quantity_scale", 100000000);
    add("quote_scale", 100000000);
    add("initial_quote_atoms", scenario.initial_quote_atoms);
    add("initial_base_atoms", scenario.initial_base_atoms);
    add("initial_base_cost_quote_atoms", scenario.initial_base_cost_atoms);
    add("fee_ppm", scenario.config.fee_ppm);
    add("quantity_step_atoms", scenario.config.quantity_step_atoms);
    add("max_order_quantity_atoms", scenario.config.max_order_quantity_atoms);
    add("max_position_quantity_atoms", scenario.config.max_position_quantity_atoms);
    add("stale_after_ns", scenario.config.stale_after_ns);
    add("order_latency_ns", scenario.config.order_latency_ns);
    add("report_latency_ns", scenario.config.report_latency_ns);
    if (const auto& s = scenario.strategy) {
        add("strategy", "threshold");
        add("buy_price_atoms", s->buy_price_atoms);
        add("sell_price_atoms", s->sell_price_atoms);
        add("strategy_quantity_atoms", s->quantity_atoms);
        add("buy_budget_quote_atoms", s->buy_budget_quote_atoms);
        add("stop_loss_ppm", s->stop_loss_ppm);
        add("cooldown_ns", s->cooldown_ns);
    } else {
        add("strategy", "script");
        add("script_orders", scenario.orders.size());
    }
    add("events", totals.events);
    add("requests", totals.requests);
    add("rejected", totals.rejected);
    add("fills", totals.fills);
    add("fees_quote_atoms", totals.fees);
    add("cancelled_no_trusted_book", totals.cancelled_no_book);
    add("cancelled_session_ended", totals.cancelled_at_end);
    const auto& balances = account.balances();
    const auto& position = account.position();
    add("quote_total_atoms", balances.quote_total);
    add("base_total_atoms", balances.base_total);
    add("cost_basis_quote_atoms", position.base_cost_quote);
    add("realized_pnl_quote_atoms", position.realized_pnl_quote);
    // Empty values mean "unavailable": BTC is held but no trusted bid was ever seen.
    std::optional<accounting::Valuation> value;
    if (mark) value = accounting::value_at(account, mark->price_atoms);
    const bool flat = balances.base_total == 0;
    const auto field = [&](auto member, std::int64_t flat_value) {
        return value ? std::format("{}", (*value).*member) : flat ? std::format("{}", flat_value) : std::string();
    };
    rows.emplace_back("mark_price_atoms", mark ? std::format("{}", mark->price_atoms) : std::string());
    rows.emplace_back("mark_event_ordinal", mark ? std::format("{}", mark->ordinal) : std::string());
    rows.emplace_back("market_value_quote_atoms", field(&accounting::Valuation::market_value_quote, 0));
    rows.emplace_back("unrealized_pnl_quote_atoms", field(&accounting::Valuation::unrealized_pnl_quote, 0));
    rows.emplace_back("total_pnl_quote_atoms", field(&accounting::Valuation::total_pnl_quote, position.realized_pnl_quote));
    rows.emplace_back("equity_quote_atoms", field(&accounting::Valuation::equity_quote, balances.quote_total));
    add("orders_file", csv(std::filesystem::path(orders_path_).filename().string()));
    add("fills_file", csv(std::filesystem::path(fills_path_).filename().string()));
    add("status", "complete");

    File summary(std::fopen(summary_path_.c_str(), "wx"));
    if (!summary) throw std::runtime_error(std::format("cannot create new report '{}': {}", summary_path_, std::strerror(errno)));
    write(summary.get(), "key,value\n");
    for (const auto& [key, text] : rows) write(summary.get(), std::format("{},{}\n", key, text));
    if (std::fclose(summary.release()) != 0) throw std::runtime_error("report close failed");
}
} // namespace tickforge::app
