#include "commands.hpp"
#include "paper_journal.hpp"
#include "paper_reporter.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/engine/session.hpp>
#include <tickforge/console/console.hpp>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>

namespace tickforge::app {
void paper_session(const PaperOptions& options)
{
    const std::string_view session_path = options.session, scenario_path = options.config;
    const bool automatic = options.automatic;
    const auto scenario = load_paper_scenario(scenario_path, options.overrides);
    if (automatic != scenario.strategy.has_value())
        throw std::runtime_error("strategy command requires a [strategy] section; paper command requires [[orders]] instead");
    std::ifstream input{std::string(session_path)};
    if (!input) throw std::runtime_error(std::format("cannot open session '{}'", session_path));
    // Created before the run so that an existing report fails fast and is never overwritten.
    std::optional<PaperExport> exporter;
    if (!options.report_prefix.empty()) exporter.emplace(options.report_prefix);
    PaperJournal journal(scenario, exporter, {});
    engine::Session session({scenario.config, scenario.risk, scenario.initial_quote_atoms, scenario.initial_base_atoms,
                             scenario.initial_base_cost_atoms}, make_strategy(scenario), journal);
    journal.attach(session);
    std::size_t next = 0, next_cancel = 0;
    paper_header(session_path, scenario_path, scenario);
    if (automatic) write_console(strategy_header(scenario));
    replay(input, [&](const MarketEvent& event) {
        session.on_event(event);
        if (automatic) return;
        while (next < scenario.orders.size() && scenario.orders[next].after_ordinal == event.time.ordinal)
            session.submit(scenario.orders[next++].order, "script");
        // Scripted cancels follow the orders of the same event.
        while (next_cancel < scenario.cancels.size() && scenario.cancels[next_cancel].after_ordinal == event.time.ordinal) {
            const auto id = scenario.cancels[next_cancel++].order_id;
            write_console(session.cancel(id) ? std::format("Cancel    | order #{} | requested\n", id)
                                             : std::format("Cancel    | order #{} | nothing open to cancel\n", id));
        }
    });
    if (!automatic && (next != scenario.orders.size() || next_cancel != scenario.cancels.size()))
        throw std::runtime_error("scenario references an event beyond the recorded session");
    session.finish();
    journal.complete(automatic ? "strategy" : "paper", session_path, scenario_path);
}
} // namespace tickforge::app
