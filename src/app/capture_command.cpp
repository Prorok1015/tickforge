#include "commands.hpp"
#ifdef TICKFORGE_ENABLE_LIVE
#include <tickforge/adapters/binance/capture.hpp>
#endif
#include <stdexcept>
#include <string>

namespace tickforge::app {
void capture_session(std::string_view path, unsigned seconds)
{
#ifdef TICKFORGE_ENABLE_LIVE
    adapters::binance::capture(std::string(path), seconds);
#else
    static_cast<void>(path);
    static_cast<void>(seconds);
    throw std::runtime_error("capture is disabled; configure with -DTICKFORGE_ENABLE_LIVE=ON and install its dependencies");
#endif
}
} // namespace tickforge::app
