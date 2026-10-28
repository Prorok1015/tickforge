#include <tickforge/adapters/binance/binance_protocol.hpp>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <variant>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, "expected invalid Binance input to be rejected");
}
}

int main()
{
    using namespace tickforge::adapters::binance;
    try {
        check(decimal_atoms("65000.01000000", 2) == 6500001, "decimal conversion must be exact");
        check(decimal_atoms("0.00000001", 8) == 1, "smallest quantity must be retained");
        check(decimal_atoms("92233720368547758.07", 2) == INT64_MAX, "int64 boundary must work");
        for (const auto* value : {"", "-1", "+1", "1e2", "1.", ".1", " 1", "1.001", "92233720368547758.08"})
            rejects([&] { decimal_atoms(value, 2); });
        const auto rules = parse_instrument(R"({"symbols":[{"symbol":"BTCUSDT","status":"TRADING","baseAsset":"BTC","quoteAsset":"USDT","filters":[{"filterType":"PRICE_FILTER","tickSize":"0.01000000"},{"filterType":"LOT_SIZE","stepSize":"0.00001000"}]}]})");
        check(rules.price_tick_atoms == 1 && rules.quantity_step_atoms == 1000, "metadata filters must determine tick/step");
        const auto snapshot = parse_snapshot(R"({"lastUpdateId":100,"bids":[["65000.00","0.00100000"]],"asks":[["65001.00","0.00200000"]]})", rules);
        check(snapshot.requires_bridge && snapshot.last_update_id == 100 && snapshot.bids[0].price_atoms == 6500000,
              "REST snapshot must wait for a stream bridge");
        std::int64_t exchange = 0;
        const auto depth = parse_depth(R"({"e":"depthUpdate","s":"BTCUSDT","E":1000,"U":101,"u":102,"b":[["65000.00","0.00000000"]],"a":[]})", rules, exchange);
        check(depth.first_update_id == 101 && depth.last_update_id == 102 && exchange == 1000000000 &&
              depth.bids[0].quantity_atoms == 0, "depth IDs/time/deletion must normalize correctly");
        for (const auto* body : {
             R"({"e":"depthUpdate","s":"ETHUSDT","E":1,"U":1,"u":2,"b":[],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":1.5,"U":1,"u":2,"b":[],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":9223372036854775807,"U":1,"u":2,"b":[],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":1,"U":2,"u":1,"b":[],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":1,"U":1,"u":2,"b":[["65000.00","0.00000001"]],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":1,"U":1,"u":18446744073709551615,"b":[],"a":[]})",
             R"({"e":"depthUpdate","s":"BTCUSDT","E":1,"U":1,"u":2,"b":[[65000,1]],"a":[]})", "not json"})
            rejects([&] { parse_depth(body, rules, exchange); });
        rejects([&] { parse_instrument(R"({"symbols":[]})"); });
        rejects([&] { parse_snapshot(R"({"lastUpdateId":1,"bids":[["0","1"]],"asks":[]})", rules); });
        check(is_server_shutdown(R"({"e":"serverShutdown","E":1})"), "shutdown must be recognized");
        check(is_server_shutdown(R"({"stream":"btcusdt@trade","data":{"e":"serverShutdown","E":1}})"),
              "shutdown inside a combined-stream wrapper must be recognized");
        check(!is_server_shutdown(R"({"stream":"btcusdt@trade","data":{"e":"trade"}})"), "a trade is not a shutdown");

        // Trades: "m" = buyer is maker, so the seller crossed the spread.
        const auto sold = parse_trade(R"({"e":"trade","E":2000,"s":"BTCUSDT","t":12345,"p":"65000.01","q":"0.00150000","T":1999,"m":true,"M":true})", rules, exchange);
        check(sold.price_atoms == 6500001 && sold.quantity_atoms == 150000 && sold.trade_id == 12345 &&
              sold.aggressor == tickforge::Aggressor::sell && exchange == 1999000000,
              "trade fields, aggressor, and trade time must normalize");
        const auto bought = parse_trade(R"({"e":"trade","E":2000,"s":"BTCUSDT","t":12346,"p":"65000.02","q":"0.001","T":2000,"m":false,"M":true})", rules, exchange);
        check(bought.aggressor == tickforge::Aggressor::buy, "buyer as taker is a buy aggressor");
        for (const auto* body : {
             R"({"e":"trade","s":"ETHUSDT","t":1,"p":"1.00","q":"0.001","T":1,"m":true})",
             R"({"e":"trade","s":"BTCUSDT","t":0,"p":"1.00","q":"0.001","T":1,"m":true})",
             R"({"e":"trade","s":"BTCUSDT","t":-1,"p":"1.00","q":"0.001","T":1,"m":true})",
             R"({"e":"trade","s":"BTCUSDT","t":1,"p":"1.00","q":"0.00000001","T":1,"m":true})",
             R"({"e":"trade","s":"BTCUSDT","t":1,"p":"0.00","q":"0.001","T":1,"m":true})",
             R"({"e":"trade","s":"BTCUSDT","t":1,"p":"1.00","q":"0.001","T":1,"m":"yes"})",
             R"({"e":"trade","s":"BTCUSDT","t":1,"p":"1.00","q":"0.001","m":true})",
             R"({"e":"depthUpdate","s":"BTCUSDT","t":1,"p":"1.00","q":"0.001","T":1,"m":true})"})
            rejects([&] { parse_trade(body, rules, exchange); });

        // Combined stream: one connection, dispatched by stream name.
        const auto from_trade = parse_stream_message(R"({"stream":"btcusdt@trade","data":{"e":"trade","E":5,"s":"BTCUSDT","t":7,"p":"65000.00","q":"0.001","T":4,"m":false,"M":true}})", rules);
        check(std::holds_alternative<tickforge::Trade>(from_trade.data) && from_trade.exchange_ns == 4000000 &&
              std::get<tickforge::Trade>(from_trade.data).trade_id == 7, "trade stream message must yield a trade");
        const auto from_depth = parse_stream_message(R"({"stream":"btcusdt@depth@100ms","data":{"e":"depthUpdate","s":"BTCUSDT","E":3,"U":5,"u":6,"b":[],"a":[]}})", rules);
        check(std::holds_alternative<tickforge::BookUpdate>(from_depth.data) && from_depth.exchange_ns == 3000000,
              "depth stream message must yield a book update");
        for (const auto* body : {
             R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade"}})",
             R"({"stream":"btcusdt@trade","data":{"e":"depthUpdate","s":"BTCUSDT","E":3,"U":5,"u":6,"b":[],"a":[]}})",
             R"({"e":"trade","s":"BTCUSDT","t":1,"p":"1.00","q":"0.001","T":1,"m":true})"})
            rejects([&] { parse_stream_message(body, rules); });
        std::fputs("Binance protocol checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Binance protocol check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
