#pragma once
#include <remi/render/Renderer.hpp>
#include <filesystem>
#include <memory>
#include <vector>

namespace remi {
// Fixed-topology vertex animation exported from a rigged source asset.
// The game controls world movement; clip samples contain in-place poses.
class BakedAnimation {
public:
    [[nodiscard]] static std::unique_ptr<BakedAnimation> Load(const std::filesystem::path& path);
    [[nodiscard]] std::unique_ptr<Mesh> CreateMesh(Renderer& renderer) const;
    void Update(Renderer& renderer, Mesh& mesh, bool running, double elapsedSeconds);
    [[nodiscard]] std::size_t IdleFrames() const noexcept { return idleFrames_; }
    [[nodiscard]] std::size_t RunFrames() const noexcept { return runFrames_; }
private:
    std::vector<Vec3> colors_, positions_, normals_;
    std::vector<std::uint32_t> indices_;
    std::vector<Vertex> scratch_;
    std::uint32_t idleFrames_ = 0, runFrames_ = 0;
    float fps_ = 0;
    double phase_ = 0;
    bool running_ = false;
};
}
