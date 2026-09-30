#pragma once
#include <remi/render/Renderer.hpp>
#include <span>
#include <string>
#include <vector>
namespace remi::debug {
// Tiny geometry-based diagnostics. No font file or external UI dependency.
class DebugOverlay {
public:
    void Update(Renderer& renderer,unsigned width,unsigned height,std::span<const std::string> lines);
    void Draw(Renderer& renderer) const;
    void Clear() noexcept { mesh_.reset(); }
private:
    std::unique_ptr<Mesh> mesh_;
};
}
