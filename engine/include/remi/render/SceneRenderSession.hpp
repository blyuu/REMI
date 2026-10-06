#pragma once
#include <remi/render/SceneRenderer.hpp>
#include <remi/resources/ResourceCache.hpp>
#include <functional>

namespace remi {
// Owns render resources shared by code-authored games. Game meshes and HUD
// must be released before ShutdownAndValidate; the cache is released here.
class SceneRenderSession {
public:
    using ExtraDraw = std::function<void(Renderer&)>;
    explicit SceneRenderSession(const RendererConfig& config);
    SceneRenderSession(const SceneRenderSession&) = delete;
    SceneRenderSession& operator=(const SceneRenderSession&) = delete;
    [[nodiscard]] Renderer& Device();
    [[nodiscard]] ResourceCache<Mesh>& Meshes();
    [[nodiscard]] SceneDrawStats DrawShadow(const Scene& scene, const Matrix4& lightViewProjection,
        const ExtraDraw& extra = {});
    [[nodiscard]] SceneDrawStats DrawColor(const Scene& scene, const Matrix4& viewProjection,
        const DirectionalLight& light, const ExtraDraw& extra = {});
    void RequireCleanDiagnostics();
    [[nodiscard]] ShutdownReport ShutdownAndValidate();
private:
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<ResourceCache<Mesh>> meshes_;
};
}
