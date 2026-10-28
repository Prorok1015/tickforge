#include <tickforge/engine/session.hpp>
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/core/amounts.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tickforge::engine {
namespace {
std::string_view signal_name(strategy::Signal signal)
{
    switch (signal) {
    case strategy::Signal::buy: return "buy";
    case strategy::Signal::take_profit: return "take_profit";
    case strategy::Signal::stop_loss: return "stop_loss";
    case strategy::Signal::none: break;
    }
    return "none";
}
} // namespace

Session::Session(SessionConfig config, std::unique_ptr<strategy::Strategy> strategy, SessionObserver& observer)
    : config_(config), strategy_(std::move(strategy)), observer_(observer),
      account_(config.initial_quote_atoms, config.initial_base_atoms, config.initial_base_cost_atoms),
      simulator_(config.execution),
      // Initial equity matches the PnL definition: starting USDT plus the declared BTC cost.
      initial_equity_(amount_add(config.initial_quote_atoms, config.initial_base_cost_atoms)),
      risk_(config.risk, initial_equity_), manager_(account_, simulator_, config.execution, &risk_)
{
}

void Session::collect()
{
    for (const auto& report : manager_.collect()) {
        const auto found = in_flight_.find(report.order.id);
        if (found == in_flight_.end()) throw std::runtime_error("report for an order this run did not submit");
        observer_.on_report(report, found->second.time, found->second.signal, !found->second.reported);
        found->second.reported = true;
        if (report.final) in_flight_.erase(found);
    }
}

void Session::note_risk(const EventTime& time)
{
    if (risk_.state() == risk_state_) return;
    risk_state_ = risk_.state();
    // Resting orders the new state forbids are withdrawn: buys in reduce-only, all when halted.
    const auto cancelled = manager_.cancel_open(risk_.state() == execution::TradingState::halted
        ? std::nullopt : std::optional<Side>(Side::buy));
    observer_.on_risk(time, cancelled);
}

void Session::submit_at(const EventTime& time, const Order& order, std::string_view signal)
{
    observer_.on_request(time, order);
    const auto rejected = manager_.submit(order);
    note_risk(time);
    if (rejected) {
        observer_.on_report(*rejected, time, signal, true);
    } else {
        in_flight_.emplace(order.id, Submitted{time, signal});
        observer_.on_accepted(time, order);
    }
    collect();
}

void Session::submit(const Order& order, std::string_view signal)
{
    if (!last_time_) throw std::runtime_error("orders need a market event first");
    submit_at(*last_time_, order, signal);
}

bool Session::cancel(std::int64_t order_id)
{
    const bool requested = manager_.cancel(order_id);
    collect();
    return requested;
}

void Session::on_event(const MarketEvent& event)
{
    last_time_ = event.time;
    simulator_.on_market_event(event);
    collect();
    const bool available = simulator_.available();
    const auto& book = simulator_.book();
    observer_.on_market(event, available, available ? book.best_bid() : std::nullopt,
                        available ? book.best_ask() : std::nullopt);
    // Recordings end with a shutdown reset, so keep the last trusted bid for valuation.
    if (const auto bid = book.best_bid(); bid && available) mark_ = Mark{bid->price_atoms, event.time.ordinal};
    // Risk is judged only on a trusted current valuation; a flat account needs no price.
    std::optional<std::int64_t> equity;
    if (account_.balances().base_total == 0) equity = account_.balances().quote_total;
    else if (available && mark_ && mark_->ordinal == event.time.ordinal)
        equity = accounting::value_at(account_, mark_->price_atoms).equity_quote;
    risk_.observe(event.time.receive_elapsed_ns, equity);
    note_risk(event.time);
    if (strategy_) decide(event.time);
}

void Session::decide(const EventTime& time)
{
    std::vector<strategy::OpenOrder> open;
    for (const auto& order : manager_.open_orders())
        open.push_back({order.order, order.filled_quantity_atoms, order.cancel_requested});
    const auto actions = strategy_->on_market({time, simulator_.available(),
        simulator_.executable_level(Side::sell), simulator_.executable_level(Side::buy),
        account_.balances(), account_.position(), risk_.buys_allowed(), risk_.sells_allowed(), std::move(open)});
    for (const auto& action : actions) {
        if (const auto* notice = std::get_if<strategy::Notice>(&action)) {
            observer_.on_notice(time, notice->text);
        } else if (const auto* cancel = std::get_if<strategy::Cancel>(&action)) {
            const bool requested = manager_.cancel(cancel->order_id);
            observer_.on_cancel(time, cancel->order_id, requested);
            collect();
        } else {
            const auto& place = std::get<strategy::Place>(action);
            observer_.on_signal(time, place);
            submit_at(time, place.order, signal_name(place.signal));
        }
    }
}

void Session::finish()
{
    // Orders still in flight cannot execute without market data; executed ones still report.
    simulator_.finish();
    collect();
    if (manager_.in_flight() != 0) throw std::runtime_error("orders remain unresolved after the session");
}
} // namespace tickforge::engine
