#pragma once

#include "events.hpp"
#include <cstdio>
#include <string>

namespace tickforge {

class SessionRecorder {
public:
    explicit SessionRecorder(const std::string& path);
    ~SessionRecorder();
    SessionRecorder(const SessionRecorder&) = delete;
    SessionRecorder& operator=(const SessionRecorder&) = delete;
    void append(const MarketEvent& event);
    void finish();
private:
    void write(const std::string& text);
    std::FILE* file_ = nullptr;
    std::int64_t ordinal_ = 0;
    std::int64_t elapsed_ = 0;
    bool finished_ = false;
};

} // namespace tickforge
