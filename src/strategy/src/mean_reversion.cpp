#include <tickforge/strategy/mean_reversion.hpp>
#include <tickforge/accounting/valuation.hpp>
#include <tickforge/core/amounts.hpp>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace tickforge::strategy {
namespace {
std::string price_text(std::int64_t atoms) { return std::format("{}.{:02}", atoms / 100, atoms % 100); }
std::string seconds_text(std::int64_t ns)
{
    const auto seconds = ns / 1000000000;
    return std::format("{}:{:02}", seconds / 60, seconds % 60);
}
} // namespace

void validate_config(const MeanReversionConfig& c)
{
    if (c.window_ns < MeanReversion::sample_ns || c.window_ns > 86400 * MeanReversion::sample_ns ||
        c.window_ns % MeanReversion::sample_ns != 0 || c.entry_below_ppm <= 0 || c.entry_below_ppm >= 1000000 ||
        c.take_profit_ppm <= 0 || c.take_profit_ppm > 1000000 || c.quantity_atoms <= 0 ||
        c.buy_budget_quote_atoms <= 0 || c.quantity_step_atoms <= 0 || c.quantity_atoms % c.quantity_step_atoms != 0 ||
        c.fee_ppm < 0 || c.fee_ppm > 1000000 || c.stop_loss_ppm < 0 || c.stop_loss_ppm >= 1000000 ||
        c.cooldown_ns < 0 || (c.cooldown_ns > 0 && c.stop_loss_ppm == 0) || c.max_gap_ns <= 0)
        throw std::runtime_error("invalid mean_reversion window (whole seconds, 1s..24h), entry, take-profit, quantity, "
                                 "budget, step, fee, stop-loss, cooldown, or max gap");
}

MeanReversion::MeanReversion(MeanReversionConfig config)
    : config_(config), cooldown_(config.cooldown_ns),
      window_samples_(static_cast<std::size_t>(config.window_ns / sample_ns))
{
    validate_config(config);
}

std::optional<std::int64_t> MeanReversion::average_price() const
{
    if (!warm()) return std::nullopt;
    return sum_ / static_cast<std::int64_t>(window_samples_) / 2;
}

std::optional<std::int64_t> MeanReversion::entry_price() const
{
    if (!warm()) return std::nullopt;
    // Twice the average times (1 - entry), halved; rounded down so it never buys higher.
    const auto average2 = sum_ / static_cast<std::int64_t>(window_samples_);
    return amount_multiply_divide(average2, 1000000 - config_.entry_below_ppm, 2000000, false);
}

std::string MeanReversion::status() const
{
    if (!warm())
        return std::format("warming up: {} of {} of prices collected; no buys until then",
            seconds_text(static_cast<std::int64_t>(samples_.size()) * sample_ns), seconds_text(config_.window_ns));
    return std::format("average {} over the last {} · buys at ≤ {} ({}.{:02}% below)", price_text(*average_price()),
        seconds_text(config_.window_ns), price_text(*entry_price()), config_.entry_below_ppm / 10000,
        config_.entry_below_ppm / 100 % 100);
}

void MeanReversion::sample(std::int64_t now_ns, std::int64_t mid2)
{
    const auto slot = now_ns / sample_ns;
    if (last_slot_ && slot <= *last_slot_) return;
    // Quiet seconds repeat the last mid; the first sample of a new history has none.
    const auto missing = last_slot_ ? std::min<std::int64_t>(slot - *last_slot_ - 1, static_cast<std::int64_t>(window_samples_)) : 0;
    for (std::int64_t i = 0; i < missing; ++i) {
        samples_.push_back(*last_mid2_);
        sum_ = amount_add(sum_, *last_mid2_);
    }
    samples_.push_back(mid2);
    sum_ = amount_add(sum_, mid2);
    while (samples_.size() > window_samples_) {
        sum_ = amount_subtract(sum_, samples_.front());
        samples_.pop_front();
    }
    last_slot_ = slot;
    last_mid2_ = mid2;
}

std::vector<Action> MeanReversion::on_market(const View& view)
{
    if (view.time.ordinal <= last_ordinal_) throw std::runtime_error("strategy requires increasing event ordinals");
    last_ordinal_ = view.time.ordinal;
    now_ns_ = view.time.receive_elapsed_ns;
    std::vector<Action> actions;
    if (const auto until = cooldown_.observe(view))
        actions.push_back(Notice{std::format("stop-loss closed the position; no buys until elapsed={} ns", *until)});
    if (!quotes_usable(view)) return actions;
    // A long outage makes the old average stale: forget it and watch again.
    if (last_usable_ns_ && now_ns_ - *last_usable_ns_ > config_.max_gap_ns) {
        actions.push_back(Notice{std::format("no usable prices for {} s; warming up again",
            (now_ns_ - *last_usable_ns_) / sample_ns)});
        samples_.clear();
        sum_ = 0;
        last_slot_.reset();
        last_mid2_.reset();
        announced_warm_ = false;
    }
    last_usable_ns_ = now_ns_;
    sample(now_ns_, amount_add(view.bid->price_atoms, view.ask->price_atoms));
    if (warm() && !announced_warm_) {
        announced_warm_ = true;
        actions.push_back(Notice{std::format("warm-up complete: average {}, buying at <= {}", price_text(*average_price()),
            price_text(*entry_price()))});
    }
    const auto& balances = view.balances;
    if (balances.base_reserved != 0 || balances.quote_reserved != 0) return actions;
    std::optional<Order> order;
    auto signal = Signal::none;
    if (balances.base_total == 0 && warm() && view.buys_allowed && !cooldown_.blocks_buys(now_ns_) &&
        view.ask->price_atoms <= *entry_price()) {
        const auto budget = std::min(config_.buy_budget_quote_atoms, balances.available_quote());
        if (budget > 0) order = Order{next_id_, Side::buy, config_.quantity_atoms, budget};
        signal = Signal::buy;
    } else if (balances.base_total > 0 && view.sells_allowed &&
               stop_triggered(view.bid->price_atoms, balances, view.position, config_.stop_loss_ppm)) {
        // As in Threshold: the stop leaves at market, without the bid-size cap or break-even check.
        const auto quantity = std::min(config_.quantity_atoms, balances.available_base());
        const auto rounded = quantity - quantity % config_.quantity_step_atoms;
        if (rounded > 0) order = Order{next_id_, Side::sell, rounded, 0};
        signal = Signal::stop_loss;
    } else if (balances.base_total > 0 && view.sells_allowed) {
        const auto cost = accounting::average_cost_price(balances, view.position);
        const auto target = cost ? amount_multiply_divide(*cost, 1000000 + config_.take_profit_ppm, 1000000, true) : 0;
        if (cost && view.bid->price_atoms >= target) {
            const auto quantity = std::min({config_.quantity_atoms, balances.available_base(), view.bid->quantity_atoms});
            const auto rounded = quantity - quantity % config_.quantity_step_atoms;
            if (rounded > 0 && sale_covers_cost(view.bid->price_atoms, rounded, balances, view.position, config_.fee_ppm))
                order = Order{next_id_, Side::sell, rounded, 0};
            signal = Signal::take_profit;
        }
    }
    if (order) {
        next_id_ = amount_add(next_id_, 1);
        cooldown_.sent(signal);
        actions.push_back(Place{*order, signal});
    }
    return actions;
}
} // namespace tickforge::strategy
