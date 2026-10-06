#pragma once
#include "GravityLevel.hpp"
#include <filesystem>

namespace remi::game {
struct GameProject {
    std::filesystem::path shader;
    std::filesystem::path character;
    std::filesystem::path font;
    GravityLevel level;
    [[nodiscard]] static GameProject Load(const std::filesystem::path& projectFile);
};
}
