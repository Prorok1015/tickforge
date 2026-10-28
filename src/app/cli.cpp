#include "cli.hpp"
#include "commands.hpp"
#include <tickforge/console/console.hpp>

#include <charconv>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <string_view>

namespace tickforge::app {
namespace {
void usage(std::string_view program, std::FILE* stream = stdout)
{
    write_console(std::format(
        "Usage     | {} replay SESSION\n"
        "Paper     | {} paper SESSION SCENARIO [REPORT_PREFIX]\n"
        "Strategy  | {} strategy SESSION CONFIG [REPORT_PREFIX]\n"
        "Reports   | REPORT_PREFIX-orders.csv, -fills.csv, -summary.csv (new files only)\n"
        "Capture   | {} capture NEW_SESSION [SECONDS] (requires live-enabled build)\n",
        program, program, program, program), stream);
}

unsigned duration(std::string_view text)
{
    unsigned seconds = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (error != std::errc{} || end != text.data() + text.size() || seconds == 0 || seconds > 3600)
        throw std::runtime_error("capture duration must be an integer in 1..3600 seconds");
    return seconds;
}

int dispatch(int argc, char* argv[])
{
    const std::string_view program = argv[0];
    if (argc == 1) {
        write_console("tickforge | Market research engine\nMode      | SIMULATION ONLY\n");
        usage(program);
        write_console(std::format("Example   | {} replay examples/synthetic-book.csv\n", program));
    } else if (std::string_view(argv[1]) == "replay" && argc == 3) {
        replay_session(argv[2]);
    } else if (std::string_view(argv[1]) == "capture" && (argc == 3 || argc == 4)) {
        capture_session(argv[2], argc == 4 ? duration(argv[3]) : 30);
    } else if (std::string_view(argv[1]) == "paper" && (argc == 4 || argc == 5)) {
        paper_session(argv[2], argv[3], false, argc == 5 ? argv[4] : "");
    } else if (std::string_view(argv[1]) == "strategy" && (argc == 4 || argc == 5)) {
        paper_session(argv[2], argv[3], true, argc == 5 ? argv[4] : "");
    } else {
        usage(program, stderr);
        return 2;
    }
    if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
    return 0;
}
} // namespace

int run(int argc, char* argv[])
{
    try {
        return dispatch(argc, argv);
    } catch (const std::exception& error) {
        std::fflush(stdout);
        const auto message = std::format("[ERROR] Session aborted: {}\nPartial output is not a completed session.\n", error.what());
        std::fputs(message.c_str(), stderr);
        return 1;
    }
}
} // namespace tickforge::app
