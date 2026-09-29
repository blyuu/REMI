#pragma once
#include <string_view>

namespace remi {
// Views refer to process-lifetime string literals; callers do not own them.
struct BuildInfo {
    std::string_view version;
    std::string_view configuration;
};
[[nodiscard]] BuildInfo GetBuildInfo() noexcept;
}
