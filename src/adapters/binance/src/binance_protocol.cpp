#include <tickforge/adapters/binance/binance_protocol.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>

namespace tickforge::adapters::binance {
namespace {
using Json = nlohmann::json;
constexpr auto maximum = std::numeric_limits<std::int64_t>::max();

std::int64_t id(const Json& value)
{
    if (!value.is_number_integer() || (value.is_number_unsigned() && value.get<std::uint64_t>() > maximum))
        throw std::runtime_error("expected nonnegative int64 JSON ID");
    const auto result = value.get<std::int64_t>();
    if (result < 0) throw std::runtime_error("negative JSON ID");
    return result;
}

std::vector<Level> levels(const Json& values, const InstrumentRules& rules, bool snapshot)
{
    if (!values.is_array() || values.size() > 20000) throw std::runtime_error("invalid depth array size");
    std::vector<Level> result;
    for (const auto& value : values) {
        if (!value.is_array() || value.size() != 2) throw std::runtime_error("invalid price/quantity pair");
        const auto price = decimal_atoms(value[0].get<std::string>(), 2);
        const auto quantity = decimal_atoms(value[1].get<std::string>(), 8);
        if (price <= 0 || (snapshot && quantity == 0) || rules.price_tick_atoms <= 0 ||
            rules.quantity_step_atoms <= 0 || price % rules.price_tick_atoms != 0 ||
            quantity % rules.quantity_step_atoms != 0)
            throw std::runtime_error("price/quantity violates instrument tick or step");
        result.push_back({price, quantity});
    }
    return result;
}
} // namespace

std::int64_t decimal_atoms(std::string_view text, unsigned decimals)
{
    if (text.empty() || decimals > 18) throw std::runtime_error("invalid decimal value/scale");
    std::int64_t result = 0;
    bool fraction = false;
    unsigned fractional_digits = 0;
    bool digit_before = false, digit_after = false;
    for (const char digit : text) {
        if (digit == '.' && !fraction && digit_before) { fraction = true; continue; }
        if (digit < '0' || digit > '9') throw std::runtime_error("expected unsigned decimal string");
        if (fraction) {
            digit_after = true;
            if (fractional_digits >= decimals) {
                if (digit != '0') throw std::runtime_error("decimal precision cannot be represented exactly");
                continue;
            }
            ++fractional_digits;
        } else digit_before = true;
        const auto value = digit - '0';
        if (result > (maximum - value) / 10) throw std::runtime_error("decimal int64 overflow");
        result = result * 10 + value;
    }
    if (!digit_before || (fraction && !digit_after)) throw std::runtime_error("incomplete decimal string");
    for (unsigned i = fractional_digits; i < decimals; ++i) {
        if (result > maximum / 10) throw std::runtime_error("decimal scaling overflow");
        result *= 10;
    }
    return result;
}

InstrumentRules parse_instrument(std::string_view json)
{
    const auto root = Json::parse(json);
    const auto& symbols = root.at("symbols");
    if (!symbols.is_array() || symbols.size() != 1) throw std::runtime_error("expected one instrument");
    const auto& symbol = symbols[0];
    if (symbol.at("symbol") != "BTCUSDT" || symbol.at("status") != "TRADING" ||
        symbol.at("baseAsset") != "BTC" || symbol.at("quoteAsset") != "USDT")
        throw std::runtime_error("BTCUSDT is not available for this adapter");
    InstrumentRules rules{};
    for (const auto& filter : symbol.at("filters")) {
        if (filter.at("filterType") == "PRICE_FILTER")
            rules.price_tick_atoms = decimal_atoms(filter.at("tickSize").get<std::string>(), 2);
        if (filter.at("filterType") == "LOT_SIZE")
            rules.quantity_step_atoms = decimal_atoms(filter.at("stepSize").get<std::string>(), 8);
    }
    if (rules.price_tick_atoms <= 0 || rules.quantity_step_atoms <= 0)
        throw std::runtime_error("missing/disabled tick or lot-size filter");
    return rules;
}

BookSnapshot parse_snapshot(std::string_view json, const InstrumentRules& rules)
{
    const auto root = Json::parse(json);
    return {id(root.at("lastUpdateId")), levels(root.at("bids"), rules, true),
        levels(root.at("asks"), rules, true), true};
}

BookUpdate parse_depth(std::string_view json, const InstrumentRules& rules, std::int64_t& exchange_ns)
{
    const auto root = Json::parse(json);
    if (root.at("e") != "depthUpdate" || root.at("s") != "BTCUSDT")
        throw std::runtime_error("unexpected Binance stream event/symbol");
    const auto milliseconds = id(root.at("E"));
    if (milliseconds > maximum / 1000000) throw std::runtime_error("exchange timestamp overflow");
    exchange_ns = milliseconds * 1000000;
    BookUpdate update{id(root.at("U")), id(root.at("u")), levels(root.at("b"), rules, false),
        levels(root.at("a"), rules, false)};
    if (update.first_update_id > update.last_update_id) throw std::runtime_error("reversed depth range");
    return update;
}

bool is_server_shutdown(std::string_view json)
{
    const auto root = Json::parse(json);
    return root.value("e", std::string{}) == "serverShutdown";
}
} // namespace tickforge::adapters::binance
