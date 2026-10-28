#pragma once
#include <string_view>

namespace tickforge::app {
void replay_session(std::string_view path);
void capture_session(std::string_view path, unsigned seconds);
// An empty report prefix writes no machine-readable report files.
void paper_session(std::string_view session_path, std::string_view scenario_path, bool automatic = false,
                   std::string_view report_prefix = {});
}
