#include "GravityGame.hpp"
#include "GravityLevelBuilder.hpp"
#include <remi/physics/GravityControl.hpp>
#include <cmath>
#include <stdexcept>
namespace remi::game {
GravityGame::GravityGame(MeshHandle platform,MeshHandle player,MeshHandle goal,bool separatePlayerVisual,MeshHandle coin,MeshHandle accent,GravityLevel level)
    : platformMesh_(platform),playerMesh_(player),goalMesh_(goal),coinMesh_(coin),accentMesh_(accent),separatePlayerVisual_(separatePlayerVisual),level_(level) {
    if (!std::isfinite(level_.gravity) || level_.gravity <= 0 || !std::isfinite(level_.moveSpeed) || level_.moveSpeed <= 0)
        throw std::invalid_argument("Invalid gravity level movement settings");
    Restart();
}
void GravityGame::Restart() {
    auto scene = std::make_unique<Scene>();
    PhysicsSettings settings = level_.physics; settings.gravity = {0,-level_.gravity,0};
    auto physics = std::make_unique<PhysicsWorld>(*scene,settings);
    const auto built = BuildGravityLevel(*scene,*physics,level_,
        {platformMesh_,playerMesh_,goalMesh_,coinMesh_,accentMesh_},separatePlayerVisual_);
    physics->Step(1.f/60); // Establish initial support before the first input tick.
    physics_.reset(); scene_ = std::move(scene); physics_ = std::move(physics);
    player_ = built.player; playerVisual_ = built.playerVisual; coins_ = built.coins;
    rules_.Reset({level_.coinPositions,level_.goalPosition,level_.goalHalfExtent,
        level_.deathMin,level_.deathMax});
    inverted_ = false; facingYaw_ = 0;
    playerMaterial_ = separatePlayerVisual_ && !playerMesh_.id ? MaterialPreset::Original : MaterialPreset::Silver;
    ApplyPlayerMaterial();
}
void GravityGame::ApplyPlayerMaterial() {
    auto* mesh = scene_->Get<MeshComponent>(separatePlayerVisual_ ? playerVisual_ : player_);
    if (!mesh) return;
    switch (playerMaterial_) {
    case MaterialPreset::Silver: mesh->material = {{.72f,.78f,.86f},.82f,.24f}; break;
    case MaterialPreset::Original: mesh->material = {}; break;
    case MaterialPreset::Gold: mesh->material = {{.9f,.61f,.2f},.78f,.27f}; break;
    case MaterialPreset::Midnight: mesh->material = {{.16f,.24f,.34f},.68f,.31f}; break;
    }
}
void GravityGame::CyclePlayerMaterial() {
    playerMaterial_ = static_cast<MaterialPreset>((static_cast<unsigned>(playerMaterial_)+1)%4);
    ApplyPlayerMaterial();
}
void GravityGame::Tick(Controls input,float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1.f/30 || !std::isfinite(input.x) || !std::isfinite(input.z) ||
        std::abs(input.x) > 1 || std::abs(input.z) > 1) throw std::invalid_argument("Invalid game tick input");
    if (input.restart) { Restart(); return; }
    if (rules_.Status() != State::Playing) return;
    if (input.flip && TryReverseGravity(*physics_,player_)) {
        inverted_ = !inverted_;
        rules_.RecordFlip();
    }
    const float length = std::sqrt(input.x*input.x+input.z*input.z);
    const float divisor = length > 1 ? length : 1;
    auto velocity = physics_->Velocity(player_);
    velocity.x = input.x*level_.moveSpeed/divisor; velocity.z = input.z*level_.moveSpeed/divisor;
    physics_->SetVelocity(player_,velocity); physics_->Step(dt);
    if (separatePlayerVisual_) {
        auto* visual = scene_->Get<TransformComponent>(playerVisual_);
        if (length > .001f) facingYaw_ = std::atan2(input.x,input.z);
        // The upside-down Z rotation mirrors horizontal facing in this transform order.
        visual->rotation.y = inverted_ ? -facingYaw_ : facingYaw_;
        visual->rotation.z = inverted_ ? 3.14159265f : 0;
    }
    const auto p = Position();
    for (std::size_t i = 0; i < coins_.size(); ++i) {
        if (rules_.Collected(i)) continue;
        const auto coin = coins_[i];
        scene_->Get<TransformComponent>(coin)->rotation.y += dt*2.5f;
    }
    const auto outcome = rules_.Tick(p,Supported(),inverted_,dt);
    for (std::size_t i = 0; i < coins_.size(); ++i) if (outcome.collected[i])
        if (auto* mesh = scene_->Get<MeshComponent>(coins_[i])) mesh->visible = false;
}
}
