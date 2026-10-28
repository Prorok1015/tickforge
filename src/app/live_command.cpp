#include "commands.hpp"
#ifdef TICKFORGE_ENABLE_LIVE
#include "live_dashboard.hpp"
#include "paper_journal.hpp"
#include "paper_reporter.hpp"
#include <tickforge/adapters/binance/live_feed.hpp>
#include <tickforge/engine/session.hpp>
#include <tickforge/console/console.hpp>
#include <unistd.h>
#include <cstdio>
#include <format>
#endif
#include <stdexcept>

namespace tickforge::app {
void live_session(const LiveOptions& options)
{
#ifdef TICKFORGE_ENABLE_LIVE
    const auto scenario = load_paper_scenario(options.config, options.overrides);
    if (!scenario.has_strategy()) throw std::runtime_error("live command requires a [strategy] section");
    std::optional<PaperExport> exporter;
    if (!options.report_prefix.empty()) exporter.emplace(options.report_prefix);
    // The full-screen view needs a terminal; piped output gets the plain journal.
    const bool screen = !options.plain && isatty(fileno(stdout)) == 1;
    PaperJournal journal(scenario, exporter, {.journal = !screen, .market_lines = false});
    LiveDashboard dashboard(scenario, journal, static_cast<std::int64_t>(options.seconds) * 1000000000);
    engine::SessionObserver& observer = screen ? static_cast<engine::SessionObserver&>(dashboard) : journal;
    engine::Session session({scenario.config, scenario.risk, scenario.initial_quote_atoms, scenario.initial_base_atoms,
                             scenario.initial_base_cost_atoms}, make_strategy(scenario), observer);
    journal.attach(session);
    dashboard.attach(session);
    const auto source = options.record_path.empty() ? std::string("live Binance BTCUSDT (not recorded)")
                                                    : std::format("live Binance BTCUSDT, recorded to {}", options.record_path);
    if (!screen) {
        paper_header(source, options.config, scenario);
        write_console(strategy_header(scenario));
    } else {
        dashboard.open();
    }
    adapters::binance::FeedOptions feed{options.seconds, std::nullopt, false};
    if (!options.record_path.empty()) feed.record_path = options.record_path;
    // The session sees exactly the events the recording holds, in the same order.
    const auto summary = adapters::binance::run_feed(feed, [&](const MarketEvent& event) { session.on_event(event); },
        [&](std::string_view text) {
            if (screen) dashboard.note(text);
            else write_console(text);
        });
    session.finish();
    dashboard.close();
    if (screen) {
        paper_header(source, options.config, scenario);
        write_console(strategy_header(scenario));
    }
    write_console(std::format("Live      | {} events ({} trades) | {} reconnects\n", summary.events, summary.trades,
                              summary.reconnects));
    journal.complete("live", options.record_path.empty() ? std::string_view("live") : options.record_path, options.config);
    if (!options.record_path.empty()) {
        std::string command = std::format("tickforge strategy {} --config {}", options.record_path, options.config);
        for (const auto& item : options.overrides) command += " --set " + item;
        write_console(std::format("Replay    | {} reproduces this run exactly\n", command));
    }
#else
    static_cast<void>(options);
    throw std::runtime_error("live is disabled; configure with -DTICKFORGE_ENABLE_LIVE=ON and install its dependencies");
#endif
}
} // namespace tickforge::app
