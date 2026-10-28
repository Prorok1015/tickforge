#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace tickforge::app {
void replay_session(std::string_view path);
void capture_session(std::string_view path, unsigned seconds);

struct PaperOptions {
    bool automatic = false;          // strategy command: decisions come from [strategy]
    std::string session;
    std::string config;
    std::vector<std::string> overrides; // --set key=value, in command-line order
    std::string report_prefix;       // empty: no machine-readable report files
};
void paper_session(const PaperOptions& options);
}
