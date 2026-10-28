#include "live_dashboard.hpp"
#include "paper_reporter.hpp"
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/console/console.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <stdexcept>
#include <variant>

namespace tickforge::app {
namespace {
constexpr std::size_t width = 80;        // frame width in terminal columns
constexpr std::size_t recent_lines = 7;
constexpr std::size_t book_levels = 5;   // per side
constexpr std::int64_t redraw_ns = 200000000; // five frames per second of session time
constexpr auto red = "\x1b[31m", green = "\x1b[32m", yellow = "\x1b[33m", cyan = "\x1b[36m";
constexpr auto bold = "\x1b[1m", dim = "\x1b[2m", reset = "\x1b[0m";

// Terminal columns of a line: escape sequences take none, every UTF-8 character one.
std::size_t columns(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b') {
            while (i < text.size() && !((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z'))) ++i;
            continue;
        }
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::string repeat(std::string_view piece, std::size_t times)
{
    std::string out;
    for (std::size_t i = 0; i < times; ++i) out += piece;
    return out;
}

// A titled box spanning the frame width.
std::vector<std::string> panel(std::string_view title, const std::vector<std::string>& body)
{
    std::vector<std::string> lines;
    lines.push_back(std::format("{}┌─ {}{}{}{} {}┐{}", dim, reset, bold, title, dim,
        repeat("─", width - 5 - columns(title)), reset));
    for (const auto& line : body) {
        const auto used = columns(line);
        lines.push_back(std::format("{}│{} {}{} {}│{}", dim, reset, line, std::string(used < width - 4 ? width - 4 - used : 0, ' '),
            dim, reset));
    }
    lines.push_back(std::format("{}└{}┘{}", dim, repeat("─", width - 2), reset));
    return lines;
}

// 81264.00 -> "81 264.00": digit groups make five-digit prices readable at a glance.
std::string grouped(std::string text)
{
    const auto dot = text.find('.');
    auto end = dot == std::string::npos ? text.size() : dot;
    const std::size_t first = !text.empty() && (text[0] == '-' || text[0] == '+') ? 1 : 0;
    while (end > first + 3) {
        end -= 3;
        text.insert(end, " ");
    }
    return text;
}

std::string price(std::int64_t atoms) { return grouped(price_text(atoms)); }
// BTC to the venue's 0.00001 step.
std::string btc(std::int64_t atoms) { return quantity_text(atoms).substr(0, quantity_text(atoms).size() - 3); }
// USDT to 1e-4: enough for small paper results, short enough to read.
std::string usdt(std::int64_t atoms) { return grouped(money_text(atoms).substr(0, money_text(atoms).size() - 4)); }
std::string signed_usdt(std::int64_t atoms)
{
    const auto text = signed_money_text(atoms);
    return grouped(text.substr(0, text.size() - 4));
}
std::string colored(std::int64_t value, const std::string& text)
{
    return std::format("{}{}{}", value > 0 ? green : value < 0 ? red : "", text, reset);
}

std::string clock_text(std::int64_t ns)
{
    const auto seconds = std::max<std::int64_t>(ns, 0) / 1000000000;
    return seconds >= 3600 ? std::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60)
                           : std::format("{:02}:{:02}", seconds / 60, seconds % 60);
}
} // namespace

LiveDashboard::LiveDashboard(const PaperScenario& scenario, PaperJournal& journal, std::int64_t duration_ns)
    : scenario_(scenario), journal_(journal), duration_ns_(duration_ns)
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
    // Only the notes a person needs, reworded; the plain journal keeps the technical text.
    if (text.starts_with("Metadata")) remember("Connected to Binance (public market data only)");
    else if (text.starts_with("Snapshot")) remember("Loading the order book");
    else if (text.starts_with("Reconnect"))
        remember(std::format("{}Connection lost; reconnecting{} ({})", yellow, reset, text.substr(12, text.find('\n') - 12)));
}

void LiveDashboard::remember(std::string line)
{
    recent_.push_back(std::format("{}{:>5}{}  {}", dim, clock_text(now_ns_), reset, line));
    while (recent_.size() > recent_lines) recent_.pop_front();
}

void LiveDashboard::on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask)
{
    journal_.on_market(event, available, bid, ask);
    now_ns_ = event.time.receive_elapsed_ns;
    if (available && !connected_) remember(std::format("{}Market data live{}", green, reset));
    if (const auto* reset_event = std::get_if<BookReset>(&event.data);
        reset_event && connected_ && reset_event->reason != "shutdown")
        remember(std::format("{}Market data interrupted{}: orders stay; new ones wait for the book", yellow, reset));
    connected_ = available ? true : std::holds_alternative<BookReset>(event.data) ? false : connected_;
    available_ = available;
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
        const bool buy = fill.side == Side::buy;
        remember(std::format("{}{} {} BTC at {}{}  {}", buy ? green : red, buy ? "Bought" : "Sold  ", btc(fill.quantity_atoms),
            price(fill.price_atoms), reset,
            fill.liquidity == Liquidity::maker ? (buy ? "our bid was hit by a seller" : "our ask was lifted by a buyer")
                                               : (buy ? "we took the ask" : "we hit the bid")));
    }
    if (!report.final) return;
    if (report.order.side == Side::sell && report.filled_quantity_atoms > 0) ++round_trips_;
    if (report.result.status == OrderStatus::rejected)
        remember(std::format("{}Order #{} rejected{}: {}", red, report.order.id, reset, report.result.reason));
    else if (report.filled_quantity_atoms == 0)
        remember(std::format("Order #{} cancelled, nothing filled", report.order.id));
}

void LiveDashboard::on_request(const EventTime& time, const Order& order) { journal_.on_request(time, order); }
void LiveDashboard::on_accepted(const EventTime& time, const Order& order) { journal_.on_accepted(time, order); }

void LiveDashboard::on_risk(const EventTime& time, std::size_t cancelled)
{
    journal_.on_risk(time, cancelled);
    remember(std::format("{}Risk limit reached ({}){}: {}", red, execution::trigger_name(session_->risk().trigger()), reset,
        session_->risk().state() == execution::TradingState::halted ? "trading stopped" : "selling only from now on"));
}

void LiveDashboard::on_signal(const EventTime& time, const strategy::Place& place)
{
    journal_.on_signal(time, place);
    const auto& order = place.order;
    const bool buy = order.side == Side::buy;
    if (place.signal == strategy::Signal::stop_loss)
        remember(std::format("{}Stop-loss{}: selling {} BTC at market", red, reset, btc(order.quantity_atoms)));
    else if (order.type == OrderType::limit)
        remember(std::format("Placed {} {} BTC at {}", buy ? "BUY" : "SELL", btc(order.quantity_atoms), price(order.limit_price_atoms)));
    else
        remember(std::format("{} {} BTC at market", buy ? "Buying" : "Selling", btc(order.quantity_atoms)));
}

void LiveDashboard::on_cancel(const EventTime& time, std::int64_t order_id, bool requested)
{
    journal_.on_cancel(time, order_id, requested);
    if (requested) remember(std::format("Cancelling order #{}", order_id));
}

void LiveDashboard::on_notice(const EventTime& time, std::string_view text)
{
    journal_.on_notice(time, text);
    remember(std::format("{}{}{}", yellow, text, reset));
}

std::vector<std::string> LiveDashboard::result_panel() const
{
    const auto& session = *session_;
    const auto& position = session.account().position();
    const auto start = session.initial_equity();
    std::int64_t unrealized = 0, equity = session.account().balances().quote_total;
    if (const auto mark = session.mark()) {
        const auto value = accounting::value_at(session.account(), mark->price_atoms);
        unrealized = value.unrealized_pnl_quote;
        equity = value.equity_quote;
    }
    const auto total = position.realized_pnl_quote + unrealized;
    // Basis points of the starting money, shown as a signed percentage with two decimals.
    const auto bp = start == 0 ? 0 : total * 10000 / start;
    const auto percent = std::format("{}{}.{:02}%", total < 0 ? "-" : "+", std::abs(bp) / 100, std::abs(bp) % 100);
    return {
        std::format("{}{} USDT{}  {}   = closed trades {}  + open position {}", bold,
            colored(total, signed_usdt(total)), reset, colored(total, percent),
            colored(position.realized_pnl_quote, signed_usdt(position.realized_pnl_quote)),
            colored(unrealized, signed_usdt(unrealized))),
        std::format("{}worth {} USDT · started {} · {} round trips · fees {}{}", dim, usdt(equity),
            usdt(start), round_trips_, usdt(journal_.totals.fees), reset),
    };
}

std::vector<std::string> LiveDashboard::status_panel() const
{
    const auto& session = *session_;
    const auto& s = *scenario_.strategy;
    std::vector<std::string> lines;
    const auto bid = session.book().best_bid(), ask = session.book().best_ask();
    const auto& balances = session.account().balances();
    const auto open_orders = session.open_orders();
    const auto& risk = session.risk();
    if (risk.state() != execution::TradingState::normal) {
        lines.push_back(std::format("{}■ Risk limit reached: {}{}", red,
            risk.state() == execution::TradingState::halted ? "no more orders" : "only selling is allowed", reset));
    } else if (!available_) {
        lines.push_back(std::format("{}■ Waiting for market data{} (connecting or resynchronizing); no new orders", yellow, reset));
    }
    for (const auto& open : open_orders) {
        const auto& order = open.order;
        const bool buy = order.side == Side::buy;
        const auto left = order.quantity_atoms - open.filled_quantity_atoms;
        if (order.type != OrderType::limit) {
            lines.push_back(std::format("{}▶{} {} {} BTC at market: on its way to the exchange", cyan, reset,
                buy ? "Buying" : "Selling", btc(left)));
            continue;
        }
        lines.push_back(std::format("{}▶{} Waiting to {} {} BTC at {}{}{}", cyan, reset, buy ? "buy" : "sell", btc(left),
            bold, price(order.limit_price_atoms), reset));
        const auto best = buy ? bid : ask;
        if (open.cancel_requested) {
            lines.push_back("  cancel requested; it may still fill until the cancel arrives");
        } else if (best && best->price_atoms == order.limit_price_atoms) {
            const auto ahead = session.queue_ahead(order.id);
            lines.push_back(std::format("  we are the best {} · about {} BTC in the queue ahead of us", buy ? "bid" : "ask",
                ahead ? btc(*ahead) : std::string("?")));
        } else if (best) {
            const auto gap = buy ? best->price_atoms - order.limit_price_atoms : order.limit_price_atoms - best->price_atoms;
            // A resting order does not chase the market; it waits for the price to come back.
            if (gap > 0)
                lines.push_back(std::format("  the market moved away: the price must {} {} USDT ({}.{:02}%) to reach us",
                    buy ? "fall" : "rise", price(gap), gap * 10000 / best->price_atoms / 100,
                    gap * 10000 / best->price_atoms % 100));
            else
                lines.push_back("  ahead of the best price: first in line at a new level");
        }
    }
    if (open_orders.empty() && available_ && risk.state() == execution::TradingState::normal) {
        if (balances.base_total > 0)
            lines.push_back(std::format("{}▶{} Holding {} BTC; waiting for a sell price of at least {}", cyan, reset,
                btc(balances.base_total), price(s.sell_price_atoms)));
        else if (ask && ask->price_atoms > s.buy_price_atoms)
            lines.push_back(std::format("{}▶{} Waiting to buy: the best ask {} is above our buy price {}", cyan, reset,
                price(ask->price_atoms), price(s.buy_price_atoms)));
        else
            lines.push_back(std::format("{}▶{} Ready to buy", cyan, reset));
    }
    lines.push_back(std::format("{}rules: buy at ≤ {} · sell at ≥ {}, never below cost{}", dim,
        price(s.buy_price_atoms), price(s.sell_price_atoms), reset));
    lines.push_back(std::format("{}       {} BTC per order · {}{}", dim, btc(s.quantity_atoms),
        scenario_.strategy_type == "threshold_maker" ? "limit orders that wait in the book (maker)"
                                                     : "market orders that take the best price (taker)", reset));
    return lines;
}

std::vector<std::string> LiveDashboard::market_panel() const
{
    const auto& session = *session_;
    const auto& book = session.book();
    auto asks = book.ask_depth(), bids = book.bid_depth();
    if (asks.size() > book_levels) asks.resize(book_levels);
    if (bids.size() > book_levels) bids.resize(book_levels);
    if (asks.empty() || bids.empty()) return {std::format("{}no order book yet{}", dim, reset)};
    std::int64_t largest = 1;
    for (const auto& level : asks) largest = std::max(largest, level.quantity_atoms);
    for (const auto& level : bids) largest = std::max(largest, level.quantity_atoms);
    // Our resting limit orders by side and price.
    std::map<std::pair<Side, std::int64_t>, std::int64_t> ours;
    for (const auto& open : session.open_orders())
        if (open.order.type == OrderType::limit)
            ours[{open.order.side, open.order.limit_price_atoms}] += open.order.quantity_atoms - open.filled_quantity_atoms;
    const auto row = [&](const Level& level, Side side) {
        const auto bar = static_cast<std::size_t>(std::max<std::int64_t>(1, level.quantity_atoms * 24 / largest));
        const auto found = ours.find({side == Side::sell ? Side::sell : Side::buy, level.price_atoms});
        const auto mark = found == ours.end() ? std::string()
            : std::format("  {}◀ our {} {}{}", bold, side == Side::sell ? "ask" : "bid", btc(found->second), reset);
        return std::format("{}{:>12}{}  {:>11}  {}{}{}{}", side == Side::sell ? red : green, price(level.price_atoms), reset,
            btc(level.quantity_atoms), side == Side::sell ? red : green, repeat("█", bar), reset, mark);
    };
    std::vector<std::string> lines;
    lines.push_back(std::format("{}{:>12}  {:>11}  sellers above, buyers below{}", dim, "price USDT", "size BTC", reset));
    for (auto level = asks.rbegin(); level != asks.rend(); ++level) lines.push_back(row(*level, Side::sell));
    lines.push_back(std::format("{}{:>12}  spread {} USDT{}", dim, "", price(asks.front().price_atoms - bids.front().price_atoms),
        reset));
    for (const auto& level : bids) lines.push_back(row(level, Side::buy));
    // Orders outside the shown levels are named, so they never silently disappear.
    for (const auto& [key, quantity] : ours) {
        const auto& [side, at] = key;
        const bool shown = side == Side::sell ? at <= asks.back().price_atoms : at >= bids.back().price_atoms;
        if (!shown)
            lines.push_back(std::format("{}our {} {} BTC at {} is {} the shown levels{}", dim, side == Side::sell ? "ask" : "bid",
                btc(quantity), price(at), side == Side::sell ? "above" : "below", reset));
    }
    return lines;
}

std::vector<std::string> LiveDashboard::money_panel() const
{
    const auto& balances = session_->account().balances();
    const auto average = accounting::average_cost_price(balances, session_->account().position());
    return {
        std::format("USDT {:>14}   {}free {}{} · locked in orders {}", usdt(balances.quote_total), dim, reset,
            usdt(balances.available_quote()), usdt(balances.quote_reserved)),
        std::format("BTC  {:>14}   {}free {}{} · locked in orders {}{}", btc(balances.base_total), dim, reset,
            btc(balances.available_base()), btc(balances.base_reserved),
            average ? std::format(" · bought at {} on average", price(*average)) : std::string()),
    };
}

void LiveDashboard::render()
{
    if (!session_) return;
    std::vector<std::string> lines;
    // Title row: what this is, whether data flows, and how long is left.
    const auto progress = duration_ns_ <= 0 ? 0 : static_cast<std::size_t>(std::min<std::int64_t>(16, now_ns_ * 16 / duration_ns_));
    const auto title = std::format("{}tickforge · LIVE PAPER TRADING · BTCUSDT{}", bold, reset);
    const auto state = std::format("{}● {}{}  {} / {} {}{}{}{}", available_ ? green : yellow,
        available_ ? "live" : "no data", reset, clock_text(now_ns_), clock_text(duration_ns_), cyan, repeat("▰", progress),
        repeat("▱", 16 - progress), reset);
    lines.push_back(title + std::string(width > columns(title) + columns(state) ? width - columns(title) - columns(state) : 1, ' ') + state);
    lines.push_back(std::format("{}virtual money only: nothing is sent to the exchange{}", dim, reset));
    for (const auto& line : panel("RESULT", result_panel())) lines.push_back(line);
    for (const auto& line : panel("BOT", status_panel())) lines.push_back(line);
    for (const auto& line : panel("MARKET", market_panel())) lines.push_back(line);
    for (const auto& line : panel("WALLET", money_panel())) lines.push_back(line);
    std::vector<std::string> activity(recent_.begin(), recent_.end());
    if (activity.empty()) activity.push_back(std::format("{}nothing yet{}", dim, reset));
    for (const auto& line : panel("ACTIVITY", activity)) lines.push_back(line);
    lines.push_back(std::format("{}Ctrl+C{} {} stop and save: open orders are cancelled, results are written{}", bold,
        reset, dim, reset));

    std::string frame = "\x1b[H";
    for (const auto& line : lines) frame += line + "\x1b[K\n";
    frame += "\x1b[J";
    write_console(frame);
    if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
}
} // namespace tickforge::app
