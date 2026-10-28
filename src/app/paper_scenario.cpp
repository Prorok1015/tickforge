#include "paper_scenario.hpp"
#include <charconv>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

namespace tickforge::app {
namespace {
std::vector<std::string_view> fields(std::string_view line)
{
    std::vector<std::string_view> result;
    for (;;) {
        const auto comma = line.find(',');
        result.push_back(line.substr(0, comma));
        if (comma == std::string_view::npos) return result;
        line.remove_prefix(comma + 1);
    }
}
std::int64_t integer(std::string_view text)
{
    std::int64_t value = 0;
    if (text.empty() || text.find_first_not_of("0123456789") != std::string_view::npos)
        throw std::runtime_error("expected nonnegative decimal integer");
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) throw std::runtime_error("scenario integer overflow");
    return value;
}
}

PaperScenario load_paper_scenario(std::string_view path)
{
    std::ifstream input{std::string(path)};
    if (!input) throw std::runtime_error(std::format("cannot open scenario '{}'", path));
    PaperScenario result{};
    std::string line;
    std::size_t number = 0;
    std::int64_t previous = 0;
    std::set<std::int64_t> ids;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            const auto values = fields(line);
            if (number == 1) {
                if (line != "tickforge-orders,1,BTCUSDT") throw std::runtime_error("unsupported scenario header");
            } else if (number == 2 && (values.size() == 3 || values.size() == 4) && values[0] == "account") {
                result.initial_quote_atoms = integer(values[1]);
                result.initial_base_atoms = integer(values[2]);
                result.initial_base_cost_atoms = values.size() == 4 ? integer(values[3]) : 0;
                if (values.size() == 3 && result.initial_base_atoms != 0)
                    throw std::runtime_error("initial BTC requires its cost basis in USDT atoms");
                if (result.initial_base_atoms == 0 && result.initial_base_cost_atoms != 0)
                    throw std::runtime_error("cost basis requires initial BTC");
            } else if (number == 3 && values.size() == 6 && values[0] == "config") {
                result.config = {integer(values[1]), integer(values[2]), integer(values[3]), integer(values[4]), integer(values[5])};
                execution::validate_config(result.config);
            } else if (number == 4 && values.size() == 5 && values[0] == "threshold") {
                result.strategy = strategy::ThresholdConfig{integer(values[1]), integer(values[2]),
                    integer(values[3]), integer(values[4]), result.config.quantity_step_atoms, result.config.fee_ppm};
                strategy::validate_config(*result.strategy);
            } else if (number > 3 && !result.strategy && values.size() == 6 && values[0] == "order") {
                if (values[3] != "buy" && values[3] != "sell") throw std::runtime_error("expected buy or sell");
                ScriptOrder request{integer(values[1]), {integer(values[2]), values[3] == "buy" ? Side::buy : Side::sell,
                    integer(values[4]), integer(values[5])}};
                if (request.after_ordinal < 1 || request.after_ordinal < previous || request.order.id < 1 ||
                    !ids.insert(request.order.id).second) throw std::runtime_error("invalid scenario order or duplicate ID");
                previous = request.after_ordinal;
                result.orders.push_back(request);
            } else throw std::runtime_error("unexpected scenario row or field count");
        } catch (const std::exception& error) {
            throw std::runtime_error(std::format("scenario line {}: {}", number, error.what()));
        }
    }
    if (input.bad() || (input.fail() && !input.eof())) throw std::runtime_error("scenario read failed");
    if (number < 3) throw std::runtime_error("incomplete scenario header/account/config");
    return result;
}
} // namespace tickforge::app
