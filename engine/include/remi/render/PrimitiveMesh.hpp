#pragma once
#include <remi/render/Renderer.hpp>

namespace remi {
[[nodiscard]] std::unique_ptr<Mesh> CreateBoxMesh(Renderer& renderer, Vec3 color);
}
