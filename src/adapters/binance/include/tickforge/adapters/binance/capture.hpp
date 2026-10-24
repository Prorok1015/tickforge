#pragma once
#include <string>

namespace tickforge::adapters::binance {
// Public depth capture only. No authentication or order-submission code exists.
void capture(const std::string& path, unsigned seconds);
}
