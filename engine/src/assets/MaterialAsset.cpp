#include <remi/assets/MaterialAsset.hpp>
#include "json.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace remi::assets {
namespace {
const char* ModelName(ShadingModel model) {
    switch (model) {
    case ShadingModel::Standard: return "Standard";
    case ShadingModel::Unlit: return "Unlit";
    case ShadingModel::Toon: return "Toon";
    default: throw std::invalid_argument("Unknown shading model");
    }
}
ShadingModel ParseModel(const std::string& name) {
    if (name == "Standard") return ShadingModel::Standard;
    if (name == "Unlit") return ShadingModel::Unlit;
    if (name == "Toon") return ShadingModel::Toon;
    throw std::runtime_error("Unknown shading model: " + name);
}
void Validate(const MaterialAsset& material) {
    const auto& p = material.properties;
    if (p.shadingModel >= ShadingModel::Count || !std::isfinite(p.tint.x) || !std::isfinite(p.tint.y) ||
        !std::isfinite(p.tint.z) || p.tint.x < 0 || p.tint.y < 0 || p.tint.z < 0 ||
        !std::isfinite(p.metallic) || p.metallic < 0 || p.metallic > 1 ||
        !std::isfinite(p.roughness) || p.roughness < .04f || p.roughness > 1 ||
        !std::isfinite(p.emissive) || p.emissive < 0 || p.emissive > 2)
        throw std::invalid_argument("Invalid material properties");
}
}
MaterialAsset LoadMaterial(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open material file");
    const auto json = nlohmann::json::parse(input);
    if (json.at("version").get<int>() != 1) throw std::runtime_error("Unsupported material version");
    MaterialAsset asset;
    asset.properties.shadingModel = ParseModel(json.value("shadingModel", std::string("Standard")));
    auto tint = json.value("tint", std::array<float,3>{1,1,1});
    asset.properties.tint = {tint[0], tint[1], tint[2]};
    asset.properties.metallic = json.value("metallic", 0.f);
    asset.properties.roughness = json.value("roughness", .6f);
    asset.properties.emissive = json.value("emissive", 0.f);
    const auto texture = json.value("baseColorTexture", std::string());
    if (!texture.empty()) {
        const auto relative = std::filesystem::path(std::u8string(texture.begin(), texture.end()));
        asset.baseColorTexture = relative.is_absolute() ? relative : (file.parent_path() / relative).lexically_normal();
    }
    Validate(asset);
    return asset;
}
void SaveMaterial(const std::filesystem::path& file, const MaterialAsset& asset) {
    Validate(asset);
    auto reference = asset.baseColorTexture;
    if (!reference.empty() && reference.is_absolute()) {
        const auto relative = reference.lexically_relative(file.parent_path());
        if (!relative.empty()) reference = relative;
    }
    const auto utf8 = reference.generic_u8string();
    const nlohmann::json json = {
        {"version", 1}, {"shadingModel", ModelName(asset.properties.shadingModel)},
        {"tint", {asset.properties.tint.x, asset.properties.tint.y, asset.properties.tint.z}},
        {"metallic", asset.properties.metallic}, {"roughness", asset.properties.roughness},
        {"emissive", asset.properties.emissive},
        {"baseColorTexture", std::string(utf8.begin(), utf8.end())}};
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto temporary = file; temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json.dump(2) << '\n';
        if (!output) throw std::runtime_error("Could not write material file");
    }
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Could not replace material file");
    }
}
}
