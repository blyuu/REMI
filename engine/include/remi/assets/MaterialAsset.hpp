#pragma once
#include <remi/render/Material.hpp>
#include <filesystem>

namespace remi::assets {
struct MaterialAsset {
    MaterialProperties properties;
    std::filesystem::path baseColorTexture;
};
// Versioned, human-editable material file. Relative texture paths resolve from the file.
[[nodiscard]] MaterialAsset LoadMaterial(const std::filesystem::path& file);
void SaveMaterial(const std::filesystem::path& file, const MaterialAsset& material);
}
