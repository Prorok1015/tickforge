#include <tickforge/accounting/account.hpp>
#include <tickforge/core/amounts.hpp>
#include <cstdio>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "expected rejection");
}
}

int main()
{
    using namespace tickforge;
    using namespace tickforge::accounting;
    try {
        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        check(amount_multiply_divide(maximum, 100, 100, false) == maximum, "intermediate product must not overflow");
        check(amount_multiply_divide(maximum, 1000000000, 1000000000, true) == maximum, "large divisor boundary");
        check(amount_multiply_divide(maximum, 2, 100, false) == maximum / 50, "large floor division");
        check(amount_multiply_divide(maximum, 2, 100, true) == maximum / 50 + 1, "large ceiling division");
        for (std::int64_t left = 0; left < 200; left += 13)
            for (std::int64_t right = 0; right < 180; right += 11)
                for (const std::int64_t divisor : {1, 3, 100, 1000000}) {
                    const auto product = left * right;
                    check(amount_multiply_divide(left, right, divisor, false) == product / divisor, "floor arithmetic");
                    check(amount_multiply_divide(left, right, divisor, true) == product / divisor + (product % divisor != 0), "ceiling arithmetic");
                }
        rejects([&] { amount_add(maximum, 1); });
        rejects([&] { amount_multiply(maximum, 2); });
        rejects([&] { amount_multiply_divide(maximum, 101, 100, false); });
        rejects([&] { amount_multiply_divide(1, 1, 1000000001, false); });
        rejects([&] { amount_subtract(1, 2); });

        for (std::int64_t value = 0; value < 120; value += 7)
            for (std::int64_t denominator = 1; denominator < 60; denominator += 4)
                for (std::int64_t numerator = 0; numerator <= denominator; numerator += 3) {
                    const auto product = value * numerator;
                    check(amount_fraction(value, numerator, denominator, false) == product / denominator, "floor fraction");
                    check(amount_fraction(value, numerator, denominator, true) ==
                          product / denominator + (product % denominator != 0), "ceiling fraction");
                }
        check(amount_fraction(maximum, maximum - 1, maximum, false) == maximum - 1, "fraction with a huge denominator");
        check(amount_fraction(maximum, 1, 3, true) == maximum / 3 + 1, "huge value fraction rounds up");
        check(amount_fraction(maximum, maximum, maximum, true) == maximum, "whole fraction is exact");
        rejects([&] { amount_fraction(1, 2, 1, false); });
        rejects([&] { amount_fraction(1, 0, 0, false); });
        rejects([&] { amount_fraction(-1, 0, 1, false); });
        constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
        check(signed_add(-5, 3) == -2 && signed_subtract(3, 5) == -2, "signed arithmetic");
        check(signed_add(maximum, minimum) == -1 && signed_subtract(-1, maximum) == minimum, "signed boundaries");
        rejects([&] { signed_add(maximum, 1); });
        rejects([&] { signed_add(minimum, -1); });
        rejects([&] { signed_subtract(minimum, 1); });
        rejects([&] { signed_subtract(0, minimum); });

        rejects([&] { Account invalid(-1, 0, 0); });
        rejects([&] { Account invalid(0, 0, 1); });
        rejects([&] { Account invalid(0, 1, -1); });

        Account account(100000000000, 0, 0); // 1000 USDT
        account.reserve({1, Side::buy, 300000, 30000000000});
        check(account.balances().quote_reserved == 30000000000 && account.balances().available_quote() == 70000000000,
              "buy reservation must reduce available funds, not total cash");
        check(account.pending_buy_quantity() == 300000, "pending buy quantity must be tracked");
        const Fill purchase{1, 1, Side::buy, 6500100, 100000, 6500100000, 6500100};
        check(account.apply_fill(purchase), "first fill must apply");
        check(account.balances().quote_total == 93493399900 && account.balances().base_total == 100000 &&
              account.balances().quote_reserved == 23493399900 && account.pending_buy_quantity() == 200000,
              "buy must debit notional plus fee and reduce its reserve");
        check(account.position() == Position{6506600100, 0}, "buy cost basis includes the commission");
        auto before = account.balances();
        check(!account.apply_fill(purchase) && account.balances() == before &&
              account.position() == Position{6506600100, 0}, "duplicate fill must be idempotent");
        auto conflicting = purchase; ++conflicting.fee_quote_atoms;
        rejects([&] { account.apply_fill(conflicting); });
        check(account.balances() == before, "conflicting receipt must not change money");
        check(account.release(1) && !account.release(1), "reserve release must be idempotent");
        check(account.balances().quote_reserved == 0 && account.pending_buy_quantity() == 0,
              "terminal order must release all unused funds");
        check(!account.apply_fill(purchase), "duplicate must remain harmless after closing order");
        auto late = purchase; late.id = 2;
        rejects([&] { account.apply_fill(late); });
        rejects([&] { account.reserve({1, Side::buy, 1000, 1}); });

        account.reserve({2, Side::sell, 100000, 0});
        check(account.balances().available_base() == 0 && account.balances().base_total == 100000,
              "sell reservation must not spend BTC before a fill");
        const Fill sale{2, 2, Side::sell, 6500000, 100000, 6500000000, 6500000};
        check(account.apply_fill(sale), "sell fill must apply");
        check(account.balances().quote_total == 99986899900 && account.balances().base_total == 0 &&
              account.balances().base_reserved == 0, "sell credits proceeds net of commission");
        check(account.position() == Position{0, -13100100}, "round trip realizes spread and both commissions");
        check(account.balances().quote_total + account.position().base_cost_quote -
              account.position().realized_pnl_quote == 100000000000, "cash plus cost minus realized PnL is conserved");
        account.release(2);
        before = account.balances();
        rejects([&] { account.reserve({3, Side::sell, 1, 0}); });
        rejects([&] { account.reserve({3, Side::buy, 1, 100000000000}); });
        check(account.balances() == before, "failed reservations must not alter balances");

        Account overflow(100, maximum, 0);
        overflow.reserve({1, Side::buy, 1, 10});
        before = overflow.balances();
        rejects([&] { overflow.apply_fill({1, 1, Side::buy, 100, 1, 1, 0}); });
        check(overflow.balances() == before, "overflowing fill must be atomic");
        Account bounded(100, 0, 0);
        bounded.reserve({1, Side::buy, 1, 10});
        before = bounded.balances();
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 1, 10, 1}); });
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 2, 1, 0}); });
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 1, 0, 0}); });
        check(bounded.balances() == before, "invalid fills must leave all reserves intact");

        // Average cost across two buy prices, then partial and full sales.
        Account average(100000, 0, 0);
        average.reserve({10, Side::buy, 300, 10000});
        average.apply_fill({10, 10, Side::buy, 1000, 100, 1000, 0});
        average.apply_fill({11, 10, Side::buy, 1000, 200, 2001, 0});
        average.release(10);
        check(average.position() == Position{3001, 0}, "cost basis accumulates every purchase");
        average.reserve({11, Side::sell, 300, 0});
        average.apply_fill({12, 11, Side::sell, 1100, 100, 1100, 0});
        check(average.position() == Position{2000, 99}, "a third of the cost is removed, rounded against profit");
        average.apply_fill({13, 11, Side::sell, 950, 200, 1900, 0});
        check(average.position() == Position{0, -1}, "closing removes the exact remaining cost");
        check(average.balances().quote_total == 100000 - 3001 + 1100 + 1900, "cash matches realized result");
        Account inherited(0, 100000000, 6500000000000); // 1 BTC with a 65000 USDT cost
        inherited.reserve({1, Side::sell, 100000000, 0});
        inherited.apply_fill({1, 1, Side::sell, 6600000, 100000000, 6600000000000, 0});
        check(inherited.position() == Position{0, 100000000000}, "initial inventory uses its declared cost");
        std::fputs("Accounting and checked amount checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Accounting check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
