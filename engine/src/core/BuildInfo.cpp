#include <remi/core/BuildInfo.hpp>

namespace remi {
BuildInfo GetBuildInfo() noexcept {
    return {REMI_VERSION, REMI_CONFIGURATION};
}
}
