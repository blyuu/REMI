#include <remi/render/SceneRenderer.hpp>

namespace remi {
SceneDrawStats DrawScene(Renderer& renderer, const Scene& scene, const Matrix4& viewProjection, const MeshResolver& resolve, const DirectionalLight* light, bool cull) {
    SceneDrawStats stats;
    scene.ForEachEntity([&](EntityId entity) {
        const auto* component = scene.Get<MeshComponent>(entity);
        if (!component) return;
        if (!component->visible) { ++stats.hidden; return; }
        const auto* mesh = resolve ? resolve(component->mesh) : nullptr;
        if (!mesh) { ++stats.missing; return; }
        const auto world = scene.WorldMatrix(entity); const auto mvp = Multiply(world,viewProjection);
        if (cull && !mesh->IntersectsClip(mvp)) { ++stats.culled; return; }
        if (light) renderer.DrawLitPrepared(*mesh,world,mvp,*light,component->material);
        else renderer.Draw(*mesh,mvp);
        ++stats.draws;
    });
    return stats;
}
}
