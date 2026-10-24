#include "recorder.hpp"
#include <cerrno>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

namespace tickforge {

SessionRecorder::SessionRecorder(const std::string& path)
{
    file_ = std::fopen(path.c_str(), "wx");
    if (!file_) throw std::runtime_error(std::format("cannot create new recording '{}': {}", path, std::strerror(errno)));
    try {
        write("tickforge,3,BINANCE_SPOT,BTCUSDT,price_scale=100,quantity_scale=100000000\n");
    } catch (...) {
        std::fclose(file_);
        file_ = nullptr;
        throw;
    }
}

SessionRecorder::~SessionRecorder() { if (file_) std::fclose(file_); }

void SessionRecorder::write(const std::string& text)
{
    if (std::fwrite(text.data(), 1, text.size(), file_) != text.size() || std::fflush(file_) != 0)
        throw std::runtime_error("recording write/flush failed");
}

void SessionRecorder::append(const MarketEvent& event)
{
    const auto& t = event.time;
    if (finished_ || ordinal_ == std::numeric_limits<std::int64_t>::max() ||
        t.ordinal != ordinal_ + 1 || t.receive_elapsed_ns < elapsed_ ||
        t.exchange_ns < 0 || t.receive_unix_ns < 0)
        throw std::runtime_error("invalid recording event order/timestamps");
    std::string row;
    const auto prefix = std::format(",{},{},{},{}", t.ordinal, t.exchange_ns, t.receive_unix_ns, t.receive_elapsed_ns);
    if (const auto* trade = std::get_if<Trade>(&event.data)) {
        if (trade->price_atoms <= 0 || trade->quantity_atoms <= 0) throw std::runtime_error("invalid trade");
        row = std::format("trade{},{},{}", prefix, trade->price_atoms, trade->quantity_atoms);
    } else if (const auto* reset = std::get_if<BookReset>(&event.data)) {
        if (reset->reason.empty() || reset->reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos)
            throw std::runtime_error("invalid reset reason");
        row = std::format("reset{},{}", prefix, reset->reason);
    } else {
        const std::vector<Level>* bids;
        const std::vector<Level>* asks;
        if (const auto* snapshot = std::get_if<BookSnapshot>(&event.data)) {
            if (snapshot->last_update_id < 0) throw std::runtime_error("invalid snapshot ID");
            row = std::format("{}{},{},{},{}", snapshot->requires_bridge ? "snapshot_pending" : "snapshot",
                prefix, snapshot->last_update_id, snapshot->bids.size(), snapshot->asks.size());
            bids = &snapshot->bids;
            asks = &snapshot->asks;
        } else {
            const auto& update = std::get<BookUpdate>(event.data);
            if (update.first_update_id < 0 || update.last_update_id < update.first_update_id)
                throw std::runtime_error("invalid depth ID range");
            row = std::format("depth{},{},{},{},{}", prefix, update.first_update_id, update.last_update_id,
                update.bids.size(), update.asks.size());
            bids = &update.bids;
            asks = &update.asks;
        }
        for (const auto* side : {bids, asks})
            for (const auto& level : *side) {
                if (level.price_atoms <= 0 || level.quantity_atoms < 0)
                    throw std::runtime_error("invalid recorded level");
                row += std::format(",{},{}", level.price_atoms, level.quantity_atoms);
            }
    }
    write(row + '\n');
    ordinal_ = t.ordinal;
    elapsed_ = t.receive_elapsed_ns;
}

void SessionRecorder::finish()
{
    if (finished_) throw std::runtime_error("recording already finished");
    write(std::format("end,{}\n", ordinal_));
    finished_ = true;
}
} // namespace tickforge
