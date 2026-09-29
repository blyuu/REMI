#pragma once
#include <iostream>
#include <string_view>

namespace remi {
// Single-threaded Phase 1 sink. Do not log per frame.
inline void Log(std::string_view message) { std::clog << "[REMI] " << message << '\n'; }
}
