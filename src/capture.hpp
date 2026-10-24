#pragma once
#include <string>

namespace tickforge {
// Public depth capture only. No authentication or order-submission code exists.
void capture_binance(const std::string& path, unsigned seconds);
}
