#include <remi/render/SceneRenderSession.hpp>
#include <stdexcept>

namespace remi {
SceneRenderSession::SceneRenderSession(const RendererConfig& config)
    : renderer_(std::make_unique<Renderer>(config)),
      meshes_(std::make_unique<ResourceCache<Mesh>>()) {}

Renderer& SceneRenderSession::Device() {
    if (!renderer_) throw std::logic_error("Render session is shut down");
    return *renderer_;
}

ResourceCache<Mesh>& SceneRenderSession::Meshes() {
    if (!meshes_) throw std::logic_error("Render session is shut down");
    return *meshes_;
}

SceneDrawStats SceneRenderSession::DrawShadow(const Scene& scene, const Matrix4& lightViewProjection,
    const ExtraDraw& extra) {
    auto& renderer = Device();
    auto& meshes = Meshes();
    renderer.BeginShadow(lightViewProjection);
    SceneDrawStats stats;
    try {
        stats = DrawScene(renderer,scene,lightViewProjection,
            [&](MeshHandle handle) { return meshes.Get(handle); });
        if (extra) extra(renderer);
    } catch (...) {
        renderer.EndShadow();
        throw;
    }
    renderer.EndShadow();
    if (stats.missing) throw std::runtime_error("Shadow pass has unresolved scene meshes");
    return stats;
}

SceneDrawStats SceneRenderSession::DrawColor(const Scene& scene, const Matrix4& viewProjection,
    const DirectionalLight& light, const ExtraDraw& extra) {
    auto& renderer = Device();
    auto& meshes = Meshes();
    const auto stats = DrawScene(renderer,scene,viewProjection,
        [&](MeshHandle handle) { return meshes.Get(handle); },&light);
    if (extra) extra(renderer);
    if (stats.missing) throw std::runtime_error("Color pass has unresolved scene meshes");
    return stats;
}

void SceneRenderSession::RequireCleanDiagnostics() {
    if (Device().CheckDiagnostics()) throw std::runtime_error("D3D11 render diagnostics failed");
}

ShutdownReport SceneRenderSession::ShutdownAndValidate() {
    if (!renderer_) throw std::logic_error("Render session is already shut down");
    meshes_.reset();
    auto report = renderer_->ShutdownAndValidate();
    renderer_.reset();
    return report;
}
}
