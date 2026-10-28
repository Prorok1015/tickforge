#include "live_dashboard.hpp"
#include "paper_reporter.hpp"
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/console/console.hpp>
#include <algorithm>
#include <cstdio>
#include <format>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

namespace tickforge::app {
namespace {
constexpr std::size_t recent_lines = 8;
constexpr std::int64_t redraw_ns = 200000000; // five frames per second of session time
constexpr auto red = "\x1b[31m", green = "\x1b[32m", bold = "\x1b[1m", dim = "\x1b[2m", reset = "\x1b[0m";

std::string clock_text(std::int64_t ns)
{
    const auto seconds = ns / 1000000000;
    return std::format("{:02}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

std::string_view side_text(Side side) { return side == Side::buy ? "BUY" : "SELL"; }
} // namespace

LiveDashboard::LiveDashboard(const PaperScenario& scenario, PaperJournal& journal, std::int64_t duration_ns,
                             std::size_t depth)
    : scenario_(scenario), journal_(journal), duration_ns_(duration_ns), depth_(depth)
{
}

LiveDashboard::~LiveDashboard()
{
    // Never leave the terminal in the alternate screen, even when a run aborts.
    if (open_) std::fputs("\x1b[?25h\x1b[?1049l", stdout);
}

void LiveDashboard::open()
{
    write_console("\x1b[?1049h\x1b[?25l\x1b[2J");
    open_ = true;
}

void LiveDashboard::close()
{
    if (!open_) return;
    render();
    write_console("\x1b[?25h\x1b[?1049l");
    open_ = false;
}

void LiveDashboard::note(std::string_view text)
{
    // Feed notes may hold several lines; keep the informative ones.
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        if (end > start) remember(std::string(text.substr(start, end - start)));
        start = end + 1;
    }
}

void LiveDashboard::remember(std::string line)
{
    recent_.push_back(std::move(line));
    while (recent_.size() > recent_lines) recent_.pop_front();
}

void LiveDashboard::on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask)
{
    journal_.on_market(event, available, bid, ask);
    now_ns_ = event.time.receive_elapsed_ns;
    available_ = available;
    if (std::holds_alternative<Trade>(event.data)) ++trades_;
    if (const auto* reset = std::get_if<BookReset>(&event.data))
        remember(std::format("{} | book reset: {}", clock_text(now_ns_), reset->reason));
    if (open_ && (last_draw_ns_ < 0 || now_ns_ - last_draw_ns_ >= redraw_ns)) {
        render();
        last_draw_ns_ = now_ns_;
    }
}

void LiveDashboard::on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                              bool first)
{
    journal_.on_report(report, submitted, signal, first);
    for (const auto& fill : report.result.fills) {
        std::string evidence;
        for (const auto& note : report.diagnostics)
            if (note.fill_id == fill.id) evidence = std::format(" via {}", execution::source_name(note.source));
        remember(std::format("{} | FILL #{} {} {} BTC at {} | {}{}", clock_text(now_ns_), report.order.id,
            side_text(fill.side), quantity_text(fill.quantity_atoms), price_text(fill.price_atoms),
            fill.liquidity == Liquidity::maker ? "maker" : "taker", evidence));
    }
    if (report.final && report.result.fills.empty())
        remember(std::format("{} | order #{} ended: {}", clock_text(now_ns_), report.order.id, report.result.reason));
}

void LiveDashboard::on_request(const EventTime& time, const Order& order) { journal_.on_request(time, order); }
void LiveDashboard::on_accepted(const EventTime& time, const Order& order) { journal_.on_accepted(time, order); }

void LiveDashboard::on_risk(const EventTime& time, std::size_t cancelled)
{
    journal_.on_risk(time, cancelled);
    remember(std::format("{} | RISK {} after {}", clock_text(now_ns_),
        session_->risk().state() == execution::TradingState::halted ? "halted" : "reduce-only",
        execution::trigger_name(session_->risk().trigger())));
}

void LiveDashboard::on_signal(const EventTime& time, const strategy::Place& place)
{
    journal_.on_signal(time, place);
    const auto& order = place.order;
    remember(std::format("{} | {} #{} {} {} BTC{}", clock_text(now_ns_),
        place.signal == strategy::Signal::stop_loss ? "STOP-LOSS" : "SIGNAL", order.id, side_text(order.side),
        quantity_text(order.quantity_atoms),
        order.type == OrderType::limit ? std::format(" limit {}", price_text(order.limit_price_atoms)) : " at market"));
}

void LiveDashboard::on_cancel(const EventTime& time, std::int64_t order_id, bool requested)
{
    journal_.on_cancel(time, order_id, requested);
    if (requested) remember(std::format("{} | cancel requested for #{}", clock_text(now_ns_), order_id));
}

void LiveDashboard::on_notice(const EventTime& time, std::string_view text)
{
    journal_.on_notice(time, text);
    remember(std::format("{} | {}", clock_text(now_ns_), text));
}

void LiveDashboard::render()
{
    if (!session_) return;
    const auto& session = *session_;
    std::vector<std::string> lines;
    const auto add = [&](std::string line) { lines.push_back(std::move(line)); };
    const auto& s = *scenario_.strategy;
    add(std::format("{}tickforge live | BTCUSDT | SIMULATION ONLY: virtual funds, no orders leave this machine{}", bold, reset));
    add(std::format("Session   | {} of {} | {} events | {} trades | feed {}", clock_text(now_ns_), clock_text(duration_ns_),
        journal_.totals.events, trades_, available_ ? std::format("{}AVAILABLE{}", green, reset)
                                                    : std::format("{}BLOCKED (unsynchronized or stale){}", red, reset)));
    add(std::format("Strategy  | {} | buy <= {} | sell >= {} | {} BTC per order", scenario_.strategy_type,
        price_text(s.buy_price_atoms), price_text(s.sell_price_atoms), quantity_text(s.quantity_atoms)));
    add("");

    // Our unfinished limit orders by price, to mark them beside the book.
    std::map<std::int64_t, std::string> ours;
    const auto open_orders = session.open_orders();
    for (const auto& open : open_orders)
        if (open.order.type == OrderType::limit)
            ours[open.order.limit_price_atoms] += std::format("  {}<- our {} #{} {} BTC{}", bold, side_text(open.order.side),
                open.order.id, quantity_text(open.order.quantity_atoms - open.filled_quantity_atoms), reset);
    const auto& book = session.book();
    auto asks = book.ask_depth(), bids = book.bid_depth();
    if (asks.size() > depth_) asks.resize(depth_);
    if (bids.size() > depth_) bids.resize(depth_);
    add(std::format("{}          {:>14} {:>14}{}", dim, "PRICE USDT", "SIZE BTC", reset));
    for (auto level = asks.rbegin(); level != asks.rend(); ++level)
        add(std::format("  {}ask{}     {:>14} {:>14}{}", red, reset, price_text(level->price_atoms),
            quantity_text(level->quantity_atoms), ours.contains(level->price_atoms) ? ours[level->price_atoms] : ""));
    if (!asks.empty() && !bids.empty())
        add(std::format("{}  ------  spread {} USDT{}", dim, price_text(asks.front().price_atoms - bids.front().price_atoms), reset));
    else
        add(std::format("{}  ------  no synchronized book{}", dim, reset));
    for (const auto& level : bids)
        add(std::format("  {}bid{}     {:>14} {:>14}{}", green, reset, price_text(level.price_atoms),
            quantity_text(level.quantity_atoms), ours.contains(level.price_atoms) ? ours[level.price_atoms] : ""));
    add("");

    const auto& balances = session.account().balances();
    const auto& position = session.account().position();
    add(std::format("Account   | USDT {} total | {} reserved | {} available", money_text(balances.quote_total),
        money_text(balances.quote_reserved), money_text(balances.available_quote())));
    add(std::format("          | BTC  {} total | {} reserved | {} available", quantity_text(balances.base_total),
        quantity_text(balances.base_reserved), quantity_text(balances.available_base())));
    const auto average = accounting::average_cost_price(balances, position);
    std::string valuation = "unrealized unavailable (no trusted bid yet)";
    if (const auto mark = session.mark()) {
        const auto value = accounting::value_at(session.account(), mark->price_atoms);
        valuation = std::format("unrealized {} | total {} | equity {} | marked at bid {}",
            signed_money_text(value.unrealized_pnl_quote), signed_money_text(value.total_pnl_quote),
            money_text(value.equity_quote), price_text(mark->price_atoms));
    } else if (balances.base_total == 0) {
        valuation = std::format("total {} | equity {}", signed_money_text(position.realized_pnl_quote),
            money_text(balances.quote_total));
    }
    add(std::format("PnL       | cost {} | average {} | realized {} | {}", money_text(position.base_cost_quote),
        average ? price_text(*average) : std::string("-"), signed_money_text(position.realized_pnl_quote), valuation));
    const auto& risk = session.risk();
    add(std::format("Risk      | {} | peak equity {}",
        risk.state() == execution::TradingState::normal ? std::format("{}normal{}", green, reset)
            : std::format("{}{}{}", red, risk.state() == execution::TradingState::halted ? "HALTED" : "REDUCE-ONLY", reset),
        money_text(risk.peak_equity())));
    if (open_orders.empty()) add("Orders    | none open");
    for (std::size_t i = 0; i < open_orders.size(); ++i) {
        const auto& open = open_orders[i];
        add(std::format("{}| #{} {} {} {} BTC | filled {}{}", i == 0 ? "Orders    " : "          ", open.order.id,
            side_text(open.order.side),
            open.order.type == OrderType::limit ? std::format("LIMIT {}", price_text(open.order.limit_price_atoms)) : "MARKET",
            quantity_text(open.order.quantity_atoms), quantity_text(open.filled_quantity_atoms),
            open.cancel_requested ? " | cancel requested" : ""));
    }
    const auto& totals = journal_.totals;
    const auto& stats = totals.fill_stats;
    const auto count = [&](execution::FillSource source) { return stats.by_source[static_cast<std::size_t>(source)]; };
    add(std::format("Fills     | {} ({} maker) | fees {} | take {} | trade_through {} | book_cross {} | queue {} | near gap {}",
        totals.fills, totals.maker_fills, money_text(totals.fees), count(execution::FillSource::take),
        count(execution::FillSource::trade_through), count(execution::FillSource::book_cross),
        count(execution::FillSource::queue), stats.near_gap));
    add("");
    add(std::format("{}Recent{}", bold, reset));
    for (const auto& line : recent_) add("  " + line);
    add("");
    add(std::format("{}Ctrl+C stops: open orders end as session_ended, the recording gets its footer, reports are written{}",
        dim, reset));

    std::string frame = "\x1b[H";
    for (const auto& line : lines) frame += line + "\x1b[K\n";
    frame += "\x1b[J";
    write_console(frame);
    if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
}
} // namespace tickforge::app
