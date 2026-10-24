#pragma once

#include "events.hpp"
#include "order_book.hpp"
#include <cstdio>
#include <string>
#include <string_view>

namespace tickforge {

// Exact decimal display of nonnegative atomic values, without floating point.
std::string price_text(std::int64_t atoms);
std::string quantity_text(std::int64_t atoms);
void write_console(std::string_view text, std::FILE* stream = stdout);
std::string book_text(const OrderBook& book);
std::string_view result_text(BookResult result);

} // namespace tickforge
