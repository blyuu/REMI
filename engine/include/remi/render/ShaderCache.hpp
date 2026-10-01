#pragma once
#include <remi/render/ShadingModel.hpp>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace remi {
enum class ShaderStage : std::uint8_t { Vertex, Pixel };
struct ShaderKey {
    ShaderStage stage = ShaderStage::Vertex;
    ShadingModel shadingModel = ShadingModel::Standard;
    auto operator<=>(const ShaderKey&) const = default;
};

// Caches compiled HLSL bytecode. D3D11 shader objects belong to Renderer.
// ReloadIfChanged watches the main file; ReplaceSource supports explicit source edits.
// References returned by Get become invalid after Invalidate or a successful reload.
class ShaderCache {
public:
    ShaderCache(std::filesystem::path file, std::string source = {});
    [[nodiscard]] const std::vector<std::uint8_t>& Get(ShaderKey key);
    void Invalidate() noexcept;
    // Recompile all previously requested permutations before publishing a new source.
    // A compile failure leaves the last known-good bytecode intact.
    [[nodiscard]] bool ReloadIfChanged();
    [[nodiscard]] bool ReplaceSource(std::string source);
    [[nodiscard]] std::size_t Generation() const noexcept { return generation_; }
    [[nodiscard]] std::size_t EntryCount() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t CompilationCount() const noexcept { return compilations_; }
private:
    std::filesystem::path file_;
    std::string source_;
    std::map<ShaderKey, std::vector<std::uint8_t>> entries_;
    std::size_t compilations_ = 0;
    std::size_t generation_ = 0;
    std::string observedSource_;
};
}
