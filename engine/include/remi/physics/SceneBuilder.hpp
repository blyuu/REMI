#pragma once
#include <remi/physics/PhysicsWorld.hpp>
#include <optional>
#include <string>

namespace remi {
// Reusable code-authored scene construction. The game decides which objects exist;
// this builder consistently attaches transforms, visuals, and optional physics.
struct BoxSpawnDesc {
    std::string name;
    Vec3 position{};
    Vec3 halfExtent{.5f,.5f,.5f};
    MeshHandle mesh{};
    MaterialProperties material{};
    std::optional<BodyDesc> body;
};

class SceneBuilder {
public:
    SceneBuilder(Scene& scene, PhysicsWorld& physics) : scene_(scene), physics_(physics) {}
    [[nodiscard]] EntityId SpawnBox(const BoxSpawnDesc& desc);
    [[nodiscard]] EntityId SpawnVisualChild(std::string name, EntityId parent, MeshHandle mesh,
        TransformComponent local = {});
private:
    Scene& scene_;
    PhysicsWorld& physics_;
};
}
