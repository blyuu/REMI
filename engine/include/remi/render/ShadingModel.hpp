#pragma once
#include <cstdint>

namespace remi {
// Only these models have matching PSMain implementations in Basic.hlsl.
enum class ShadingModel : std::uint8_t { Standard, Unlit, Toon, Count };
}
