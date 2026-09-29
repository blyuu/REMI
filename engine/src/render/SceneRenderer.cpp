#include <remi/render/SceneRenderer.hpp>

namespace remi {
SceneDrawStats DrawScene(Renderer& renderer, const Scene& scene, const Matrix4& viewProjection, const MeshResolver& resolve, const DirectionalLight* light, bool cull) {
    SceneDrawStats stats;
    for (auto entity : scene.Entities()) {
        const auto* component = scene.Get<MeshComponent>(entity);
        if (!component) continue;
        if (!component->visible) { ++stats.hidden; continue; }
        const auto* mesh = resolve ? resolve(component->mesh) : nullptr;
        if (!mesh) { ++stats.missing; continue; }
        const auto world = scene.WorldMatrix(entity); const auto mvp = Multiply(world,viewProjection);
        if (cull && !mesh->IntersectsClip(mvp)) { ++stats.culled; continue; }
        if (light) renderer.DrawLit(*mesh,world,viewProjection,*light);
        else renderer.Draw(*mesh,mvp);
        ++stats.draws;
    }
    return stats;
}
}
