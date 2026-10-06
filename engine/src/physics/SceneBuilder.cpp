#include <remi/physics/SceneBuilder.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace remi {
EntityId SceneBuilder::SpawnBox(const BoxSpawnDesc& desc) {
    const auto valid = [](Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    const float maxHalf = std::numeric_limits<float>::max()/2;
    if (!valid(desc.position) || !valid(desc.halfExtent) ||
        desc.halfExtent.x <= 0 || desc.halfExtent.y <= 0 || desc.halfExtent.z <= 0 ||
        desc.halfExtent.x > maxHalf || desc.halfExtent.y > maxHalf || desc.halfExtent.z > maxHalf)
        throw std::invalid_argument("Invalid box spawn transform");
    const auto id = scene_.Create(desc.name);
    try {
        *scene_.Get<TransformComponent>(id) = {desc.position,{},
            {desc.halfExtent.x*2,desc.halfExtent.y*2,desc.halfExtent.z*2}};
        if (desc.mesh.id) scene_.Add<MeshComponent>(id,{desc.mesh,true,desc.material});
        if (desc.body) physics_.AddBody(id,*desc.body);
    } catch (...) {
        scene_.Destroy(id);
        throw;
    }
    return id;
}
EntityId SceneBuilder::SpawnVisualChild(std::string name, EntityId parent, MeshHandle mesh, TransformComponent local) {
    if (!scene_.Alive(parent)) throw std::invalid_argument("Visual child requires a live parent");
    const auto id = scene_.Create(std::move(name));
    try {
        *scene_.Get<TransformComponent>(id) = local;
        if (mesh.id) scene_.Add<MeshComponent>(id,{mesh,true});
        if (!scene_.SetParent(id,parent)) throw std::logic_error("Cannot attach visual child");
    } catch (...) {
        scene_.Destroy(id);
        throw;
    }
    return id;
}
}
