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
        rejects([&] { Account invalid(-1, 0); });

        Account account(100000000000, 0); // 1000 USDT
        account.reserve({1, Side::buy, 300000, 30000000000});
        check(account.balances().quote_reserved == 30000000000 && account.balances().available_quote() == 70000000000,
              "buy reservation must reduce available funds, not total cash");
        check(account.pending_buy_quantity() == 300000, "pending buy quantity must be tracked");
        const Fill purchase{1, 1, Side::buy, 6500100, 100000, 6500100000, 6500100};
        check(account.apply_fill(purchase), "first fill must apply");
        check(account.balances().quote_total == 93493399900 && account.balances().base_total == 100000 &&
              account.balances().quote_reserved == 23493399900 && account.pending_buy_quantity() == 200000,
              "buy must debit notional plus fee and reduce its reserve");
        auto before = account.balances();
        check(!account.apply_fill(purchase) && account.balances() == before, "duplicate fill must be idempotent");
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
        account.release(2);
        before = account.balances();
        rejects([&] { account.reserve({3, Side::sell, 1, 0}); });
        rejects([&] { account.reserve({3, Side::buy, 1, 100000000000}); });
        check(account.balances() == before, "failed reservations must not alter balances");

        Account overflow(100, maximum);
        overflow.reserve({1, Side::buy, 1, 10});
        before = overflow.balances();
        rejects([&] { overflow.apply_fill({1, 1, Side::buy, 100, 1, 1, 0}); });
        check(overflow.balances() == before, "overflowing fill must be atomic");
        Account bounded(100, 0);
        bounded.reserve({1, Side::buy, 1, 10});
        before = bounded.balances();
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 1, 10, 1}); });
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 2, 1, 0}); });
        rejects([&] { bounded.apply_fill({1, 1, Side::buy, 100, 1, 0, 0}); });
        check(bounded.balances() == before, "invalid fills must leave all reserves intact");
        std::fputs("Accounting and checked amount checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Accounting check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
