#pragma once
#include "RelayRules.hpp"
#include <remi/physics/SceneBuilder.hpp>
#include <memory>

namespace remi::relay {
struct RelayMeshes { MeshHandle floor, obstacle, player, switchMesh, goal; };
struct Controls { float x = 0, z = 0; bool restart = false; };

class RelayGame {
public:
    explicit RelayGame(RelayMeshes meshes = {}, RuleConfig rules = {});
    void Restart();
    void Tick(Controls input, float dt);
    [[nodiscard]] const Scene& World() const noexcept { return *scene_; }
    [[nodiscard]] Vec3 Position() const { return scene_->Get<TransformComponent>(player_)->position; }
    [[nodiscard]] Status Result() const noexcept { return rules_.Result(); }
    [[nodiscard]] bool SwitchActive() const noexcept { return rules_.SwitchActive(); }
    [[nodiscard]] float Remaining() const noexcept { return rules_.Remaining(); }
    [[nodiscard]] PhysicsStats Physics() const noexcept { return physics_->Stats(); }
private:
    RelayMeshes meshes_;
    RuleConfig config_;
    RelayRules rules_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<PhysicsWorld> physics_;
    EntityId player_{}, switch_{};
};
}
