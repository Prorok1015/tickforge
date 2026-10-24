#pragma once

#include <tickforge/core/events.hpp>
#include <functional>
#include <iosfwd>

namespace tickforge {

using EventHandler = std::function<void(const MarketEvent&)>;

// Calls the handler synchronously, once per validated event, in file order.
// An error aborts replay; previously delivered events are not rolled back.
void replay(std::istream& input, const EventHandler& handler);

} // namespace tickforge
