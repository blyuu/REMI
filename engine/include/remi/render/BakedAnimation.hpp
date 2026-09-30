#pragma once
#include <remi/render/Renderer.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace remi {
// Fixed-topology, in-place vertex animation. RMCH v1 (Quinn) and RMCH v2
// (named clips) are supported. This is baked animation, not skeletal skinning.
class BakedAnimation {
public:
    [[nodiscard]] static std::unique_ptr<BakedAnimation> Load(const std::filesystem::path& path);
    [[nodiscard]] std::unique_ptr<Mesh> CreateMesh(Renderer& renderer) const;
    void Update(Renderer& renderer, Mesh& mesh, std::string_view clip, double elapsedSeconds);
    void Update(Renderer& renderer, Mesh& mesh, const char* clip, double elapsedSeconds);
    void Update(Renderer& renderer, Mesh& mesh, bool running, double elapsedSeconds);
    [[nodiscard]] bool HasClip(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> ClipNames() const;
    [[nodiscard]] std::size_t IdleFrames() const noexcept;
    [[nodiscard]] std::size_t RunFrames() const noexcept;
private:
    struct Clip { std::string name; std::size_t start = 0, frames = 0; };
    std::vector<Vec3> colors_, positions_, normals_;
    std::vector<std::uint32_t> indices_;
    std::vector<Vertex> scratch_;
    std::vector<Clip> clips_;
    float fps_ = 0;
    double phase_ = 0;
    std::size_t activeClip_ = 0;
};
}
