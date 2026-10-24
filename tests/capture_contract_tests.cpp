#include "book_tracker.hpp"
#include "recorder.hpp"
#include "replay.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "expected rejection");
}
struct FileCleanup {
    std::filesystem::path path;
    ~FileCleanup() { std::error_code error; std::filesystem::remove(path, error); }
};
}

int main(int argc, char* argv[])
{
    using namespace tickforge;
    try {
        check(argc == 2, "test needs a scratch directory");
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        FileCleanup file{std::filesystem::path(argv[1]) / std::format("capture-test-{}.csv", suffix)};
        const std::vector<MarketEvent> events{
            {{1, 0, 100, 0}, BookReset{"connecting"}},
            {{2, 1, 101, 1}, BookUpdate{101, 102, {{100, 7}}, {}}},
            {{3, 2, 102, 2}, BookUpdate{103, 103, {}, {{110, 8}}}},
            {{4, 0, 103, 3}, BookSnapshot{100, {{100, 1}}, {{110, 2}}, true}},
            {{5, 0, 104, 4}, BookReset{"disconnect"}},
            {{6, 0, 105, 5}, BookSnapshot{200, {{90, 4}}, {{120, 5}}, true}},
            {{7, 3, 106, 6}, BookUpdate{199, 200, {{90, 9}}, {}}},
            {{8, 4, 107, 7}, BookUpdate{201, 202, {{90, 6}}, {}}},
            {{9, 0, 108, 8}, BookReset{"shutdown"}}
        };
        std::vector<std::string> live_states;
        auto consume = [](BookTracker& tracker, const MarketEvent& event) {
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (!std::is_same_v<T, Trade>) tracker.apply(value);
            }, event.data);
            const auto& book = tracker.view();
            return std::format("{}:{}:{}", book.synchronized(), book.last_update_id().value_or(-1),
                book.best_bid() ? book.best_bid()->quantity_atoms : -1);
        };
        {
            SessionRecorder recorder(file.path.string());
            BookTracker tracker;
            for (const auto& event : events) {
                recorder.append(event);
                live_states.push_back(consume(tracker, event));
            }
            recorder.finish();
            rejects([&] { recorder.append(events.back()); });
            rejects([&] { recorder.finish(); });
        }
        check(live_states[2] == "false:-1:-1", "pre-snapshot updates must stay hidden");
        check(live_states[3] == "true:103:7", "snapshot must replay previously received bridging updates");
        check(live_states[4] == "false:-1:-1", "disconnect must hide all quotes");
        check(live_states[5] == "false:-1:-1" && live_states[6] == "false:-1:-1",
              "snapshot and stale ranges must not establish a bridge");
        check(live_states[7] == "true:202:6", "fresh bridging range must restore availability");
        {
            std::ifstream input(file.path);
            BookTracker tracker;
            std::vector<std::string> replay_states;
            replay(input, [&](const MarketEvent& event) { replay_states.push_back(consume(tracker, event)); });
            check(replay_states == live_states, "live and replay book states must match at every event");
        }
        rejects([&] { SessionRecorder duplicate(file.path.string()); });
        std::ifstream input(file.path);
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        check(text.ends_with("end,9\n"), "completed recording needs matching footer");
        const auto parse = [](const std::string& data) {
            std::istringstream source(data);
            replay(source, [](const auto&) {});
        };
        rejects([&] { parse(text.substr(0, text.rfind("end,"))); });
        rejects([&] { parse(text + "trade,10,1,1,9,1,1\n"); });
        rejects([&] { parse(text.substr(0, text.rfind("end,")) + "end,8\n"); });

        BookTracker tracker;
        tracker.apply(BookReset{"connecting"});
        tracker.apply(BookUpdate{105, 106, {{100, 1}}, {}});
        check(tracker.apply(BookSnapshot{100, {{100, 1}}, {{110, 1}}, true}) == BookResult::gap,
              "snapshot older than the buffered bridge must be rejected");
        check(!tracker.view().synchronized() && tracker.needs_snapshot(), "bootstrap gap must require a new snapshot");
        tracker.apply(BookSnapshot{106, {{100, 1}}, {{110, 1}}, true});
        check(!tracker.view().synchronized(), "snapshot alone cannot expose a live book");
        tracker.apply(BookUpdate{107, 107, {{100, 3}}, {}});
        check(tracker.view().synchronized(), "next contiguous update establishes bridge");
        check(tracker.apply(BookUpdate{109, 109, {}, {}}) == BookResult::gap && !tracker.view().synchronized(),
              "a later gap must immediately hide quotes");
        tracker.apply(BookReset{"stale_depth"});
        check(!tracker.view().synchronized(), "stale data must invalidate quotes");
        std::fputs("Capture, synchronization, and recording checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Capture contract failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
