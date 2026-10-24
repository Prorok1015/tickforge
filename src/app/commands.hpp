#pragma once
#include <string_view>

namespace tickforge::app {
void replay_session(std::string_view path);
void capture_session(std::string_view path, unsigned seconds);
}
