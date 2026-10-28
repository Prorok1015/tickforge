#include "cli.hpp"
#include "commands.hpp"
#include <tickforge/console/console.hpp>

#include <charconv>
#include <cstdio>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tickforge::app {
namespace {
void usage(std::string_view program, std::FILE* stream = stdout)
{
    write_console(std::format(
        "Usage     | {} replay SESSION\n"
        "Paper     | {} paper SESSION --config FILE.toml [--set KEY=VALUE]... [--report PREFIX]\n"
        "Strategy  | {} strategy SESSION --config FILE.toml [--set KEY=VALUE]... [--report PREFIX]\n"
        "Overrides | --set strategy.stop_loss=0.3% replaces one config value (repeatable)\n"
        "Reports   | PREFIX-orders.csv, -fills.csv, -summary.csv (new files only)\n"
        "Capture   | {} capture NEW_SESSION [SECONDS] (requires live-enabled build)\n"
        "Live      | {} live --config FILE.toml [--set KEY=VALUE]... [--duration SECONDS] [--record NEW_SESSION]\n"
        "          |   [--report PREFIX] [--plain] (forward paper trading on the public feed; live-enabled build)\n",
        program, program, program, program, program), stream);
}

// paper/strategy arguments after the command name. nullopt means a usage error.
std::optional<PaperOptions> paper_options(int argc, char* argv[], bool automatic)
{
    if (argc < 3 || std::string_view(argv[2]).starts_with("--")) return std::nullopt;
    PaperOptions options;
    options.automatic = automatic;
    options.session = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 == argc) return std::nullopt; // every flag takes a value
        const std::string value = argv[++i];
        if (flag == "--config" && options.config.empty()) options.config = value;
        else if (flag == "--report" && options.report_prefix.empty()) options.report_prefix = value;
        else if (flag == "--set") options.overrides.push_back(value);
        else return std::nullopt;
    }
    if (options.config.empty()) return std::nullopt;
    return options;
}

unsigned duration(std::string_view text, unsigned max = 3600, std::string_view command = "capture")
{
    unsigned seconds = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (error != std::errc{} || end != text.data() + text.size() || seconds == 0 || seconds > max)
        throw std::runtime_error(std::format("{} duration must be an integer in 1..{} seconds", command, max));
    return seconds;
}

// live arguments after the command name. nullopt means a usage error.
std::optional<LiveOptions> live_options(int argc, char* argv[])
{
    LiveOptions options;
    bool timed = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--plain" && !options.plain) { options.plain = true; continue; }
        if (i + 1 == argc) return std::nullopt; // every other flag takes a value
        const std::string value = argv[++i];
        if (flag == "--config" && options.config.empty()) options.config = value;
        else if (flag == "--report" && options.report_prefix.empty()) options.report_prefix = value;
        else if (flag == "--record" && options.record_path.empty()) options.record_path = value;
        else if (flag == "--duration" && !timed) { options.seconds = duration(value, 86400, "live"); timed = true; }
        else if (flag == "--set") options.overrides.push_back(value);
        else return std::nullopt;
    }
    if (options.config.empty()) return std::nullopt;
    return options;
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
    } else if (const std::string_view command = argv[1]; command == "paper" || command == "strategy") {
        const auto options = paper_options(argc, argv, command == "strategy");
        if (!options) {
            usage(program, stderr);
            return 2;
        }
        paper_session(*options);
    } else if (command == "live") {
        const auto options = live_options(argc, argv);
        if (!options) {
            usage(program, stderr);
            return 2;
        }
        live_session(*options);
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
