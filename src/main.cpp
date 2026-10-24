#include "console.hpp"
#include "replay.hpp"
#include "book_tracker.hpp"
#ifdef TICKFORGE_ENABLE_LIVE
#include "capture.hpp"
#endif

#include <cstdio>
#include <charconv>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string_view>

int main(int argc, char* argv[])
{
    using namespace tickforge;
    try {
        if (argc == 1) {
            write_console(std::format(
                "tickforge | Market research engine\n"
                "Mode      | SIMULATION ONLY\n"
                "Usage     | {} replay SESSION\n"
                "Capture   | capture NEW_SESSION [SECONDS] (requires live-enabled build)\n"
                "Example   | {} replay examples/synthetic-book.csv\n", argv[0], argv[0]));
            return std::fflush(stdout) == 0 ? 0 : 1;
        }
        if (std::string_view(argv[1]) == "capture" && (argc == 3 || argc == 4)) {
            unsigned seconds = 30;
            if (argc == 4) {
                const std::string_view text = argv[3];
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
                if (error != std::errc{} || end != text.data() + text.size() || seconds == 0 || seconds > 3600)
                    throw std::runtime_error("capture duration must be an integer in 1..3600 seconds");
            }
#ifdef TICKFORGE_ENABLE_LIVE
            capture_binance(argv[2], seconds);
            return 0;
#else
            throw std::runtime_error("capture is disabled; configure with -DTICKFORGE_ENABLE_LIVE=ON and install its dependencies");
#endif
        }
        if (argc != 3 || std::string_view(argv[1]) != "replay") {
            write_console(std::format("[ERROR] Usage: {} replay SESSION\n", argv[0]), stderr);
            return 2;
        }
        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error(std::format("cannot open session '{}'", argv[2]));
        write_console(std::format(
            "tickforge | Offline market replay\n"
            "Source    | {}\n"
            "Market    | Binance Spot / BTCUSDT\n"
            "Mode      | SIMULATION ONLY | no network or orders\n"
            "Units     | price: USDT | quantity: BTC\n"
            "Time      | exchange/received: Unix ns | replay: monotonic session offset\n"
            "Book      | known recorded levels only; trade prices do not update the book\n\n", argv[2]));

        BookTracker tracker;
        std::size_t count = 0, trades = 0, snapshots = 0, updates = 0, resets = 0, warnings = 0;
        replay(input, [&](const MarketEvent& event) {
            ++count;
            const auto& time = event.time;
            const auto exchange_time = time.exchange_ns == 0 && !std::holds_alternative<Trade>(event.data)
                ? std::string("unavailable (snapshot/local event)") : std::format("{} ns", time.exchange_ns);
            write_console(std::format(
                "Event #{:<4} | replay +{} ns\n"
                "  TIME   | exchange={} | received={} ns\n",
                time.ordinal, time.receive_elapsed_ns, exchange_time, time.receive_unix_ns));
            if (const auto* trade = std::get_if<Trade>(&event.data)) {
                ++trades;
                write_console(std::format("  TRADE  | price={} USDT | quantity={} BTC\n",
                    price_text(trade->price_atoms), quantity_text(trade->quantity_atoms)));
            } else {
                BookResult result;
                if (const auto* snapshot = std::get_if<BookSnapshot>(&event.data)) {
                    ++snapshots;
                    write_console(std::format("  INPUT  | snapshot last_id={} | {} bid / {} ask levels\n",
                        snapshot->last_update_id, snapshot->bids.size(), snapshot->asks.size()));
                    result = tracker.apply(*snapshot);
                } else if (const auto* reset = std::get_if<BookReset>(&event.data)) {
                    ++resets;
                    write_console(std::format("  INPUT  | local reset: {}\n", reset->reason));
                    result = tracker.apply(*reset);
                } else {
                    const auto& update = std::get<BookUpdate>(event.data);
                    ++updates;
                    const auto previous_id = tracker.view().last_update_id();
                    write_console(std::format(
                        "  INPUT  | depth IDs=[{}, {}] | {} bid / {} ask changes | previous book ID={}\n",
                        update.first_update_id, update.last_update_id, update.bids.size(), update.asks.size(),
                        previous_id ? std::format("{}", *previous_id) : "unavailable"));
                    result = tracker.apply(update);
                }
                if (result == BookResult::gap || result == BookResult::invalid ||
                    result == BookResult::awaiting_snapshot) ++warnings;
                write_console(std::format("  STATUS | {}\n", result_text(result)));
            }
            write_console(book_text(tracker.view()));
            write_console("\n");
        });
        write_console(std::format(
            "Replay complete | {} events: {} trades, {} snapshots, {} depth updates\n"
            "Lifecycle       | {} local resets\n"
            "Integrity       | {} warnings | final book: {}\n",
            count, trades, snapshots, updates, resets,
            warnings, tracker.view().synchronized() ? "synchronized (known levels only)" : "unavailable"));
        if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
        return 0;
    } catch (const std::exception& error) {
        std::fflush(stdout);
        const auto message = std::format("[ERROR] Session aborted: {}\nPartial output is not a completed session.\n", error.what());
        std::fputs(message.c_str(), stderr);
        return 1;
    }
}
