#pragma once
#include <remi/render/Renderer.hpp>
#include <remi/scene/Scene.hpp>
#include <functional>

namespace remi {
struct SceneDrawStats { unsigned draws = 0, hidden = 0, missing = 0, culled = 0; };
using MeshResolver = std::function<const Mesh*(MeshHandle)>;
// Resolver must not mutate the Scene. BeginFrame/Present remain caller-owned.
[[nodiscard]] SceneDrawStats DrawScene(Renderer& renderer, const Scene& scene,
    const Matrix4& viewProjection, const MeshResolver& resolve, const DirectionalLight* light = nullptr, bool cull = true);
}
