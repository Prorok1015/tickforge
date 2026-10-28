#include <tickforge/console/replay_reporter.hpp>
#include <tickforge/console/console.hpp>
#include <format>

namespace tickforge {

ReplayReporter::ReplayReporter(std::string_view source)
{
    write_console(std::format(
        "tickforge | Offline market replay\n"
        "Source    | {}\n"
        "Market    | Binance Spot / BTCUSDT\n"
        "Mode      | SIMULATION ONLY | no network or orders\n"
        "Units     | price: USDT | quantity: BTC\n"
        "Time      | exchange/received: Unix ns | replay: monotonic session offset\n"
        "Book      | known recorded levels only; trade prices do not update the book\n\n", source));
}

void ReplayReporter::on_event(const MarketEvent& event)
{
    ++events_;
    const auto& time = event.time;
    const auto exchange_time = time.exchange_ns == 0 && !std::holds_alternative<Trade>(event.data)
        ? std::string("unavailable (snapshot/local event)") : std::format("{} ns", time.exchange_ns);
    write_console(std::format(
        "Event #{:<4} | replay +{} ns\n"
        "  TIME   | exchange={} | received={} ns\n",
        time.ordinal, time.receive_elapsed_ns, exchange_time, time.receive_unix_ns));
    std::visit([&](const auto& data) { describe(data); }, event.data);
    write_console(book_text(tracker_.view()));
    write_console("\n");
}

void ReplayReporter::describe(const Trade& trade)
{
    ++trades_;
    if (trade.trade_id == 0) {
        write_console(std::format("  TRADE  | price={} USDT | quantity={} BTC\n",
            price_text(trade.price_atoms), quantity_text(trade.quantity_atoms)));
        return;
    }
    write_console(std::format("  TRADE  | id={} | {} aggressor | price={} USDT | quantity={} BTC\n", trade.trade_id,
        trade.aggressor == Aggressor::buy ? "BUY" : "SELL", price_text(trade.price_atoms), quantity_text(trade.quantity_atoms)));
    // Venue IDs are consecutive: a jump means trades were missed (for example during a
    // reconnect). The book is unaffected, but traded volume at a price is incomplete.
    if (last_trade_id_ != 0 && trade.trade_id - 1 != last_trade_id_) { // IDs are positive: no overflow
        ++trade_gaps_;
        write_console(trade.trade_id > last_trade_id_
            ? std::format("  STATUS | TRADE GAP | ids {}..{} missing\n", last_trade_id_ + 1, trade.trade_id - 1)
            : std::format("  STATUS | TRADE ORDER | id {} after {}\n", trade.trade_id, last_trade_id_));
    }
    last_trade_id_ = trade.trade_id;
}

void ReplayReporter::describe(const BookSnapshot& snapshot)
{
    ++snapshots_;
    write_console(std::format("  INPUT  | snapshot last_id={} | {} bid / {} ask levels\n",
        snapshot.last_update_id, snapshot.bids.size(), snapshot.asks.size()));
    status(tracker_.apply(snapshot));
}

void ReplayReporter::describe(const BookUpdate& update)
{
    ++updates_;
    const auto previous_id = tracker_.view().last_update_id();
    write_console(std::format(
        "  INPUT  | depth IDs=[{}, {}] | {} bid / {} ask changes | previous book ID={}\n",
        update.first_update_id, update.last_update_id, update.bids.size(), update.asks.size(),
        previous_id ? std::format("{}", *previous_id) : "unavailable"));
    status(tracker_.apply(update));
}

void ReplayReporter::describe(const BookReset& reset)
{
    ++resets_;
    write_console(std::format("  INPUT  | local reset: {}\n", reset.reason));
    status(tracker_.apply(reset));
}

void ReplayReporter::status(BookResult result)
{
    if (result == BookResult::gap || result == BookResult::invalid || result == BookResult::awaiting_snapshot)
        ++warnings_;
    write_console(std::format("  STATUS | {}\n", result_text(result)));
}

void ReplayReporter::summary() const
{
    write_console(std::format(
        "Replay complete | {} events: {} trades, {} snapshots, {} depth updates\n"
        "Lifecycle       | {} local resets\n"
        "Trade feed      | {} ID gaps or reorders\n"
        "Integrity       | {} warnings | final book: {}\n",
        events_, trades_, snapshots_, updates_, resets_, trade_gaps_, warnings_,
        tracker_.view().synchronized() ? "synchronized (known levels only)" : "unavailable"));
}
} // namespace tickforge
