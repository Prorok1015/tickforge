#include <tickforge/core/replay.hpp>
#include <cstdio>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const std::string header =
    "tickforge,1,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\n";
void require(bool condition) { if (!condition) throw std::runtime_error("check failed"); }
std::vector<std::int64_t> run(const std::string& data)
{
    std::istringstream input(data);
    std::vector<std::int64_t> result;
    tickforge::replay(input, [&](const tickforge::MarketEvent& event) {
        // Each callback sees only the current event, with prior callbacks complete.
        require(event.time.ordinal == static_cast<std::int64_t>(result.size()) + 1);
        result.push_back(std::get<tickforge::Trade>(event.data).price_atoms);
    });
    return result;
}
void rejects(const std::string& data)
{
    bool rejected = false;
    std::istringstream input(data);
    try { tickforge::replay(input, [](const auto&) {}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
}
}
int main()
{
    try {
        const auto valid = header + "trade,1,100,200,0,6500000,1\ntrade,2,99,199,0,6500001,2\n";
        require(run(valid) == std::vector<std::int64_t>({6500000, 6500001}));
        require(run(valid) == run(valid));
        auto crlf = valid;
        for (std::size_t i = 0; (i = crlf.find('\n', i)) != std::string::npos; i += 2)
            crlf.insert(i, "\r");
        require(run(crlf) == run(valid));
        rejects(header + "trade,1,-0,0,0,1,1\n");
        require(run(header).empty());
        require(run(header + "trade,1,0,0,0,9223372036854775807,1").size() == 1);
        rejects("");
        rejects("tickforge,2\n");
        for (const auto* row : {"trade,2,0,0,0,1,1", "trade,1,0,0,0,0,1",
             "trade,1,0,0,0,1,0", "trade,1,0,0,0,-1,1", "trade,1,0,0,0,1.5,1",
             "trade,1,0,0,0,9223372036854775808,1", "trade,1,0,0,0,1,1,extra",
             "trade,1,0,0,0,1", "trade,1,0,0,0,1x,1", ""}) rejects(header + row + "\n");
        rejects(header + "trade,1,0,0,10,1,1\ntrade,2,0,0,9,1,1\n");
        rejects(header + "trade,1,0,0,0,1,1\ntrade,1,0,0,0,1,1\n");
        std::istringstream broken(header);
        broken.setstate(std::ios::badbit);
        bool failed = false;
        try { tickforge::replay(broken, [](const auto&) {}); }
        catch (const std::runtime_error&) { failed = true; }
        require(failed);
        const std::string header2 =
            "tickforge,2,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\n";
        std::istringstream mixed(header2 + "snapshot,1,0,100,0,50,1,1,100,2,110,3\n"
            "depth,2,1,101,1,51,52,1,0,100,0\ntrade,3,0,102,2,110,1\n");
        int callbacks = 0;
        tickforge::replay(mixed, [&](const tickforge::MarketEvent& event) {
            ++callbacks;
            require(event.time.ordinal == callbacks);
            if (callbacks == 1) {
                const auto& snapshot = std::get<tickforge::BookSnapshot>(event.data);
                require(snapshot.last_update_id == 50 && snapshot.bids.size() == 1 &&
                    snapshot.asks[0].price_atoms == 110 && snapshot.asks[0].quantity_atoms == 3);
                require(event.time.exchange_ns == 0 && event.time.receive_unix_ns == 100);
            } else if (callbacks == 2) {
                const auto& update = std::get<tickforge::BookUpdate>(event.data);
                require(update.first_update_id == 51 && update.last_update_id == 52 &&
                    update.bids[0].quantity_atoms == 0 && update.asks.empty());
            } else require(std::get<tickforge::Trade>(event.data).price_atoms == 110);
        });
        require(callbacks == 3);
        for (const auto* row : {"snapshot,1,0,0,0,1,2,0,100,1",
             "snapshot,1,0,0,0,1,0,0,100,1", "snapshot,1,0,0,0,1,1,0,0,1",
             "snapshot,1,0,0,0,1,9223372036854775807,1", "depth,1,0,0,0,2,1,0,0",
             "depth,1,0,0,0,1,2,0,0,extra", "unknown,1,0,0,0,1,1"}) rejects(header2 + row + "\n");
        rejects(header + "snapshot,1,0,0,0,1,0,0\n"); // v1 stays trade-only
        int delivered = 0;
        std::istringstream damaged(header2 + "trade,1,0,0,0,100,1\nmalformed\n");
        failed = false;
        try { tickforge::replay(damaged, [&](const auto&) { ++delivered; }); }
        catch (const std::runtime_error&) { failed = true; }
        require(failed && delivered == 1); // no preloading/rollback after a future error

        // v4: trades carry the venue trade ID and the aggressor side; v1-v3 trades stay unknown.
        const std::string header4 = "tickforge,4,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\n";
        std::vector<tickforge::Trade> trades;
        std::istringstream v4(header4 + "trade,1,0,0,0,6500000,100,77,buy\ntrade,2,0,0,0,6499999,200,78,sell\nend,2\n");
        tickforge::replay(v4, [&](const auto& event) { trades.push_back(std::get<tickforge::Trade>(event.data)); });
        require(trades.size() == 2 && trades[0].trade_id == 77 && trades[0].aggressor == tickforge::Aggressor::buy &&
                trades[1].aggressor == tickforge::Aggressor::sell);
        trades.clear();
        std::istringstream v3(header2 + "trade,1,0,0,0,100,1\n");
        tickforge::replay(v3, [&](const auto& event) { trades.push_back(std::get<tickforge::Trade>(event.data)); });
        require(trades.size() == 1 && trades[0].trade_id == 0 && trades[0].aggressor == tickforge::Aggressor::unknown);
        for (const auto* row : {"trade,1,0,0,0,100,1", "trade,1,0,0,0,100,1,0,buy", "trade,1,0,0,0,100,1,5,maker",
             "trade,1,0,0,0,100,1,5,BUY", "trade,1,0,0,0,100,1,-5,buy", "trade,1,0,0,0,100,1,5,buy,extra"})
            rejects(header4 + row + "\nend,1\n");
        std::fputs("Replay contract checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("{}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
