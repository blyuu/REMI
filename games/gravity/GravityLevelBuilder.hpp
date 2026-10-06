#pragma once
#include "GravityLevel.hpp"
#include <remi/physics/SceneBuilder.hpp>

namespace remi::game {
struct GravityMeshes {
    MeshHandle platform, player, goal, coin, accent;
};
struct BuiltGravityLevel {
    EntityId player, playerVisual;
    std::array<EntityId,3> coins{};
};

// Game-specific composition uses the engine's reusable SceneBuilder.
[[nodiscard]] BuiltGravityLevel BuildGravityLevel(Scene& scene, PhysicsWorld& physics,
    const GravityLevel& level, const GravityMeshes& meshes, bool separatePlayerVisual);
[[nodiscard]] GravityLevel MakeAlternateGravityLevel(GravityLevel base);
}
