#include "RelayGame.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace remi::relay {
RelayGame::RelayGame(RelayMeshes meshes, RuleConfig rules) : meshes_(meshes), config_(rules) {
    if (!std::isfinite(config_.timeLimit) || config_.timeLimit <= 0)
        throw std::invalid_argument("Invalid relay time limit");
    Restart();
}

void RelayGame::Restart() {
    auto scene = std::make_unique<Scene>();
    auto physics = std::make_unique<PhysicsWorld>(*scene);
    SceneBuilder builder(*scene,*physics);
    const auto box = [&](const char* name, Vec3 position, Vec3 half, MeshHandle mesh,
        bool collide = false, BodyType type = BodyType::Static) {
        BoxSpawnDesc desc; desc.name = name; desc.position = position;
        desc.halfExtent = half; desc.mesh = mesh;
        if (collide) { BodyDesc body; body.type = type; body.halfExtent = half; desc.body = body; }
        return builder.SpawnBox(desc);
    };
    (void)box("Relay floor",{0,-.25f,0},{8,.25f,3},meshes_.floor,true);
    (void)box("Barrier A",{-2,.7f,0},{.25f,.7f,.5f},meshes_.obstacle,true);
    (void)box("Barrier B",{3,.7f,1.2f},{.25f,.7f,.5f},meshes_.obstacle,true);
    const auto switchId = box("Relay switch",config_.switchPosition,{.45f,.1f,.45f},meshes_.switchMesh);
    (void)box("Relay exit",config_.goalPosition,{.65f,.1f,.65f},meshes_.goal);
    const auto player = box("Relay player",{-6,.4f,0},{.35f,.4f,.35f},meshes_.player,true,BodyType::Dynamic);
    physics->Step(1.f/60);
    physics_.reset(); scene_ = std::move(scene); physics_ = std::move(physics);
    player_ = player; switch_ = switchId;
    rules_.Reset(config_);
}

void RelayGame::Tick(Controls input, float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1.f/30 || !std::isfinite(input.x) ||
        !std::isfinite(input.z) || std::abs(input.x) > 1 || std::abs(input.z) > 1)
        throw std::invalid_argument("Invalid relay input");
    if (input.restart) { Restart(); return; }
    if (rules_.Result() != Status::Playing) return;
    const float length = std::sqrt(input.x*input.x+input.z*input.z);
    const float divisor = length > 1 ? length : 1;
    auto velocity = physics_->Velocity(player_);
    velocity.x = input.x*4.f/divisor; velocity.z = input.z*4.f/divisor;
    physics_->SetVelocity(player_,velocity);
    physics_->Step(dt);
    const bool wasActive = rules_.SwitchActive();
    rules_.Tick(Position(),dt);
    if (!wasActive && rules_.SwitchActive())
        if (auto* visual = scene_->Get<MeshComponent>(switch_)) visual->material.emissive = .9f;
}
}
