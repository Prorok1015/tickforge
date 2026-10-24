#include "commands.hpp"
#include <tickforge/core/replay.hpp>
#include <tickforge/console/replay_reporter.hpp>

#include <format>
#include <fstream>
#include <stdexcept>
#include <string>

namespace tickforge::app {
void replay_session(std::string_view path)
{
    std::ifstream input{std::string(path)};
    if (!input) throw std::runtime_error(std::format("cannot open session '{}'", path));
    ReplayReporter reporter(path);
    replay(input, [&](const MarketEvent& event) { reporter.on_event(event); });
    reporter.summary();
}
} // namespace tickforge::app
