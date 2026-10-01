#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace remi::assets {
struct ImageRGBA {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};
[[nodiscard]] ImageRGBA DecodeImage(std::span<const std::uint8_t> encoded);
[[nodiscard]] ImageRGBA LoadImage(const std::filesystem::path& file);
}
