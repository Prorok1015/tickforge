#pragma once
#include "paper_journal.hpp"
#include <tickforge/engine/session.hpp>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace tickforge::app {
// Full-screen terminal view of a live paper session, redrawn in place. It answers the
// questions a person watching asks, in this order: how am I doing (result), what is the
// bot doing and why (status in plain words), what does the market look like near our
// orders, where is my money, and what just happened. It forwards every callback to the
// journal, which keeps totals and reports. Redraws are paced by session time, so the
// view reads no clock either.
class LiveDashboard final : public engine::SessionObserver {
public:
    LiveDashboard(const PaperScenario& scenario, PaperJournal& journal, std::int64_t duration_ns);
    ~LiveDashboard() override;
    LiveDashboard(const LiveDashboard&) = delete;
    LiveDashboard& operator=(const LiveDashboard&) = delete;
    void attach(const engine::Session& session) { session_ = &session; }
    // Takes over the terminal (alternate screen, hidden cursor) until close().
    void open();
    // Draws the last state and gives the terminal back; safe to call twice.
    void close();
    // A feed status line (connection, snapshot, reconnect), reworded for people.
    void note(std::string_view text);

    void on_market(const MarketEvent& event, bool available, std::optional<Level> bid, std::optional<Level> ask) override;
    void on_report(const execution::OrderReport& report, const EventTime& submitted, std::string_view signal,
                   bool first) override;
    void on_request(const EventTime& time, const Order& order) override;
    void on_accepted(const EventTime& time, const Order& order) override;
    void on_risk(const EventTime& time, std::size_t cancelled) override;
    void on_signal(const EventTime& time, const strategy::Place& place) override;
    void on_cancel(const EventTime& time, std::int64_t order_id, bool requested) override;
    void on_notice(const EventTime& time, std::string_view text) override;
private:
    void remember(std::string line);
    void render();
    std::vector<std::string> result_panel() const;
    std::vector<std::string> status_panel() const;
    std::vector<std::string> market_panel() const;
    std::vector<std::string> money_panel() const;
    const PaperScenario& scenario_;
    PaperJournal& journal_;
    const engine::Session* session_ = nullptr;
    std::int64_t duration_ns_;
    std::deque<std::string> recent_;
    std::int64_t now_ns_ = 0, last_draw_ns_ = -1, round_trips_ = 0;
    bool available_ = false, connected_ = false, stopped_ = false, open_ = false;
};
} // namespace tickforge::app
