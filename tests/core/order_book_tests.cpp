#include <tickforge/core/order_book.hpp>

#include <cstdio>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
}

int main()
{
    using namespace tickforge;
    try {
        OrderBook book;
        check(!book.best_bid() && !book.last_update_id(), "new book must be unavailable");
        check(book.apply(BookUpdate{1, 2, {{100, 5}}, {}}) == BookResult::awaiting_snapshot,
              "depth before a snapshot must not create a book");
        check(book.bid_levels() == 0, "untrusted levels must not accumulate");

        check(book.apply(BookSnapshot{100, {{98, 4}, {100, 2}, {99, 3}}, {{104, 6}, {102, 5}}})
              == BookResult::snapshot, "unsorted snapshot must be accepted");
        check(book.best_bid()->price_atoms == 100 && book.best_ask()->price_atoms == 102 &&
              book.spread_atoms() == 2, "best quotes must be selected by price");
        check(book.apply(BookUpdate{101, 102, {{100, 7}, {101, 1}}, {{102, 0}, {999, 0}}})
              == BookResult::applied, "contiguous range must apply");
        check(book.best_bid()->price_atoms == 101 && book.best_ask()->price_atoms == 104,
              "insertion and deletion must change best quotes");
        check(book.apply(BookUpdate{102, 103, {{101, 9}}, {}}) == BookResult::applied,
              "overlapping range covering next ID must apply");
        check(book.best_bid()->quantity_atoms == 9, "quantity must be replaced, not added");
        check(book.apply(BookUpdate{101, 103, {{101, 50}}, {}}) == BookResult::stale,
              "equal final ID must be ignored");
        check(book.apply(BookUpdate{99, 100, {{101, 50}}, {}}) == BookResult::stale,
              "old final ID must be ignored");
        check(book.best_bid()->quantity_atoms == 9 && book.last_update_id() == 103,
              "stale ranges must not change state");

        check(book.apply(BookUpdate{105, 106, {{101, 20}}, {}}) == BookResult::gap,
              "missing ID must invalidate book");
        check(!book.synchronized() && !book.best_bid() && !book.best_ask() &&
              !book.last_update_id() && !book.spread_atoms() &&
              book.bid_levels() == 0 && book.ask_levels() == 0, "gap must hide and clear all quotes");
        check(book.apply(BookUpdate{104, 107, {{101, 20}}, {}}) == BookResult::awaiting_snapshot,
              "later depth cannot repair a gap");
        check(book.apply(BookSnapshot{200, {{110, 2}}, {{120, 3}}}) == BookResult::snapshot,
              "fresh snapshot must restore book");
        check(book.bid_levels() == 1 && book.ask_levels() == 1,
              "snapshot must replace previous levels");
        check(book.apply(BookUpdate{201, 201, {{125, 1}}, {{120, 0}, {130, 4}}}) == BookResult::applied,
              "both sides must apply atomically before checking crossing");
        check(book.spread_atoms() == 5, "spread must use final event state");
        check(book.apply(BookUpdate{202, 202, {{130, 1}}, {}}) == BookResult::invalid,
              "locked book must invalidate");
        check(!book.synchronized() && book.bid_levels() == 0, "invalid update must clear book");
        check(book.apply(BookSnapshot{300, {{100, 1}, {100, 2}}, {{110, 1}}}) == BookResult::invalid,
              "duplicate snapshot price must be rejected");
        check(book.apply(BookSnapshot{300, {{100, 0}}, {{110, 1}}}) == BookResult::invalid,
              "snapshot zero quantity must be rejected");
        check(book.apply(BookSnapshot{300, {{120, 1}}, {{110, 1}}}) == BookResult::invalid,
              "crossed snapshot must be rejected");
        check(book.apply(BookSnapshot{-1, {}, {}}) == BookResult::invalid, "negative ID must be rejected");
        check(book.apply(BookSnapshot{300, {{0, 1}}, {}}) == BookResult::invalid,
              "nonpositive prices must be rejected");
        check(book.apply(BookSnapshot{300, {{100, -1}}, {}}) == BookResult::invalid,
              "negative quantity must be rejected");
        check(book.apply(BookSnapshot{300, {{100, 1}}, {}}) == BookResult::snapshot,
              "one-sided book is representable");
        check(book.synchronized() && book.best_bid() && !book.best_ask() && !book.spread_atoms(),
              "one-sided book must not invent ask or spread");
        check(book.apply(BookUpdate{301, 301, {{100, 0}}, {}}) == BookResult::applied,
              "last level can be removed");
        check(!book.best_bid() && book.bid_levels() == 0, "empty side must have no best quote");
        check(book.apply(BookUpdate{302, 301, {}, {}}) == BookResult::invalid,
              "reversed update range must invalidate");
        check(book.apply(BookSnapshot{400, {{100, 1}}, {{110, 1}}}) == BookResult::snapshot,
              "restore after malformed update");
        check(book.apply(BookUpdate{401, 401, {{100, -1}}, {}}) == BookResult::invalid,
              "invalid quantity must invalidate an existing book");

        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        check(book.apply(BookSnapshot{maximum - 1, {{1, maximum}}, {{maximum, 1}}}) == BookResult::snapshot,
              "maximum int64 values must be representable");
        check(book.apply(BookUpdate{maximum, maximum, {}, {}}) == BookResult::applied,
              "last possible update ID must apply without overflow");
        check(book.apply(BookUpdate{maximum, maximum, {}, {}}) == BookResult::stale,
              "maximum stale ID must not overflow");
        OrderBook replacement;
        check(replacement.apply(BookSnapshot{1, {{90, 1}, {100, 2}}, {{110, 1}}}) == BookResult::snapshot,
              "initial replacement fixture");
        check(replacement.apply(BookSnapshot{10, {{80, 3}}, {{120, 4}}}) == BookResult::snapshot &&
              replacement.bid_levels() == 1 && replacement.best_bid()->price_atoms == 80 &&
              replacement.best_ask()->price_atoms == 120, "snapshot must not retain old levels");
        check(replacement.apply(BookUpdate{11, 11, {{80, 5}, {80, 6}}, {}}) == BookResult::applied &&
              replacement.best_bid()->quantity_atoms == 6, "repeated update prices use last absolute size");
        std::fputs("Order book checks passed\n", stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fputs(std::format("Order book check failed: {}\n", error.what()).c_str(), stderr);
        return 1;
    }
}
