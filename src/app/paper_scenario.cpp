#include "paper_scenario.hpp"
#include <tickforge/core/units.hpp>
#include <toml.hpp>
#include <algorithm>
#include <format>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <string>

namespace tickforge::app {
namespace {
[[noreturn]] void fail(std::string_view key, std::string_view message)
{
    throw std::runtime_error(std::format("config {}: {}", key, message));
}

// Every value is read as text and converted by the exact unit parsers. TOML floats are
// binary approximations, so decimals must be written as strings ("0.01", "0.1%").
std::string scalar_text(const toml::node& node, std::string_view key)
{
    if (const auto* text = node.as_string()) return text->get();
    if (const auto* number = node.as_integer()) {
        if (number->get() < 0) fail(key, "must not be negative");
        return std::to_string(number->get());
    }
    if (node.is_floating_point())
        fail(key, "write decimal values as strings, for example \"0.01\": TOML floats are inexact");
    fail(key, "expected a string or a nonnegative integer");
}

// A table with a fixed set of keys: unknown keys are errors, so a typo cannot silently
// leave a setting at its default.
class Section {
public:
    Section(const toml::table& table, std::string prefix, std::initializer_list<std::string_view> keys)
        : table_(table), prefix_(std::move(prefix))
    {
        for (const auto& [key, node] : table) {
            static_cast<void>(node);
            if (std::find(keys.begin(), keys.end(), key.str()) == keys.end()) fail(path(key.str()), "unknown key");
        }
    }
    std::string path(std::string_view key) const { return prefix_ + std::string(key); }
    bool has(std::string_view key) const { return table_.contains(key); }
    template<class Parse> std::int64_t get(std::string_view key, Parse parse) const
    {
        const auto* node = table_.get(key);
        if (!node) fail(path(key), "is required");
        return convert(key, *node, parse);
    }
    template<class Parse> std::int64_t get_or(std::string_view key, Parse parse, std::string_view fallback) const
    {
        const auto* node = table_.get(key);
        if (!node) return convert(key, toml::value<std::string>(std::string(fallback)), parse);
        return convert(key, *node, parse);
    }
    std::string text(std::string_view key) const
    {
        const auto* node = table_.get(key);
        if (!node || !node->is_string()) fail(path(key), "is required and must be a string");
        return node->as_string()->get();
    }
private:
    template<class Parse> std::int64_t convert(std::string_view key, const toml::node& node, Parse parse) const
    {
        const auto text = scalar_text(node, path(key));
        try {
            return parse(text);
        } catch (const std::invalid_argument& error) {
            fail(path(key), error.what());
        }
    }
    const toml::table& table_;
    std::string prefix_;
};

std::int64_t whole(std::string_view text) { return parse_decimal(text, 0); }

const toml::table* subtable(const toml::table& root, std::string_view key)
{
    const auto* node = root.get(key);
    if (!node) return nullptr;
    if (!node->is_table()) fail(key, "must be a table, for example [" + std::string(key) + "]");
    return node->as_table();
}

void apply_override(toml::table& root, const std::string& assignment)
{
    const auto equals = assignment.find('=');
    if (equals == std::string::npos || equals == 0) fail(assignment, "override must look like key=value");
    const auto key = assignment.substr(0, equals);
    const auto value = assignment.substr(equals + 1);
    auto* table = &root;
    std::size_t start = 0;
    for (;;) {
        const auto dot = key.find('.', start);
        const auto part = key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.empty() || part.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos)
            fail(key, "override keys are lowercase names separated by dots");
        if (dot == std::string::npos) {
            const auto* existing = table->get(part);
            if (existing && !existing->is_value()) fail(key, "only single values can be overridden");
            table->insert_or_assign(part, value);
            return;
        }
        auto* node = table->get(part);
        if (!node) node = &table->insert(part, toml::table{}).first->second;
        if (!node->is_table()) fail(key, "only values inside tables can be overridden");
        table = node->as_table();
        start = dot + 1;
    }
}
} // namespace

PaperScenario load_paper_scenario(std::string_view path, const std::vector<std::string>& overrides)
{
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& error) {
        const auto& where = error.source().begin;
        throw std::runtime_error(std::format("config '{}' line {}, column {}: {}", path, where.line, where.column,
                                             error.description()));
    }
    for (const auto& assignment : overrides) apply_override(root, assignment);

    PaperScenario result{};
    result.overrides = overrides;
    const Section top(root, "", {"version", "account", "execution", "risk", "strategy", "orders"});
    if (top.get("version", whole) != 1) fail("version", "only version 1 is supported");

    const auto* account_table = subtable(root, "account");
    if (!account_table) fail("account", "section is required");
    const Section account(*account_table, "account.", {"usdt", "btc", "btc_cost"});
    result.initial_quote_atoms = account.get("usdt", parse_quote);
    result.initial_base_atoms = account.get_or("btc", parse_quantity, "0");
    // Initial BTC needs an explicit cost so that selling it never shows a fictitious profit.
    if (result.initial_base_atoms > 0 && !account.has("btc_cost")) fail("account.btc_cost", "is required with initial BTC");
    result.initial_base_cost_atoms = account.get_or("btc_cost", parse_quote, "0");
    if (result.initial_base_atoms == 0 && result.initial_base_cost_atoms != 0)
        fail("account.btc_cost", "requires initial BTC");

    const auto* execution_table = subtable(root, "execution");
    if (!execution_table) fail("execution", "section is required");
    const Section execution(*execution_table, "execution.", {"fee", "quantity_step", "max_order", "max_position",
        "stale_after", "order_latency", "report_latency"});
    result.config.fee_ppm = execution.get("fee", parse_percent_ppm);
    result.config.quantity_step_atoms = execution.get("quantity_step", parse_quantity);
    result.config.max_order_quantity_atoms = execution.get("max_order", parse_quantity);
    result.config.max_position_quantity_atoms = execution.get("max_position", parse_quantity);
    result.config.stale_after_ns = execution.get("stale_after", parse_duration_ns);
    result.config.order_latency_ns = execution.get_or("order_latency", parse_duration_ns, "0s");
    result.config.report_latency_ns = execution.get_or("report_latency", parse_duration_ns, "0s");
    try {
        execution::validate_config(result.config);
    } catch (const std::invalid_argument&) {
        fail("execution", "fee must be at most 100% and the step and limits must be positive");
    }

    if (const auto* risk_table = subtable(root, "risk")) {
        const Section risk(*risk_table, "risk.", {"max_loss", "max_drawdown", "max_orders_per_minute"});
        result.risk.max_loss_quote = risk.get_or("max_loss", parse_quote, "0");
        result.risk.max_drawdown_quote = risk.get_or("max_drawdown", parse_quote, "0");
        result.risk.max_orders_per_minute = risk.get_or("max_orders_per_minute", whole, "0");
    }

    const auto* strategy_table = subtable(root, "strategy");
    const auto* orders = root.get("orders");
    if (strategy_table && orders) fail("strategy", "use either [strategy] or [[orders]], not both");
    if (strategy_table) {
        const Section strategy(*strategy_table, "strategy.", {"type", "buy_below", "sell_above", "quantity",
            "buy_budget", "stop_loss", "cooldown"});
        if (strategy.text("type") != "threshold") fail("strategy.type", "only \"threshold\" is available");
        result.strategy = strategy::ThresholdConfig{strategy.get("buy_below", parse_price),
            strategy.get("sell_above", parse_price), strategy.get("quantity", parse_quantity),
            strategy.get("buy_budget", parse_quote), result.config.quantity_step_atoms, result.config.fee_ppm,
            strategy.get_or("stop_loss", parse_percent_ppm, "0%"), strategy.get_or("cooldown", parse_duration_ns, "0s")};
        try {
            strategy::validate_config(*result.strategy);
        } catch (const std::runtime_error& error) {
            fail("strategy", error.what());
        }
    }
    if (orders) {
        if (!orders->is_array_of_tables()) fail("orders", "must be [[orders]] tables");
        std::int64_t previous = 0;
        std::set<std::int64_t> ids;
        std::size_t index = 0;
        for (const auto& node : *orders->as_array()) {
            const Section order(*node.as_table(), std::format("orders[{}].", index++),
                {"after_event", "id", "side", "quantity", "max_spend"});
            const auto side = order.text("side");
            if (side != "buy" && side != "sell") fail(order.path("side"), "must be \"buy\" or \"sell\"");
            ScriptOrder request{order.get("after_event", whole), {order.get("id", whole),
                side == "buy" ? Side::buy : Side::sell, order.get("quantity", parse_quantity), 0}};
            // A buy needs a budget including fees; a sell spends BTC, not USDT.
            if (side == "buy") request.order.max_spend_quote_atoms = order.get("max_spend", parse_quote);
            else if (order.has("max_spend")) fail(order.path("max_spend"), "applies to buys only");
            if (request.after_ordinal < 1 || request.after_ordinal < previous)
                fail(order.path("after_event"), "must be positive and nondecreasing");
            if (request.order.id < 1 || !ids.insert(request.order.id).second)
                fail(order.path("id"), "must be positive and unique");
            previous = request.after_ordinal;
            result.orders.push_back(request);
        }
    }
    return result;
}
} // namespace tickforge::app
