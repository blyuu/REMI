#pragma once
#include "GravityLevel.hpp"
#include "GravityRules.hpp"
#include <memory>
namespace remi::game {
enum class MaterialPreset { Silver, Original, Gold, Midnight };
struct Controls { float x = 0, z = 0; bool flip = false, restart = false; };
class GravityGame {
public:
    GravityGame(MeshHandle platform = {}, MeshHandle player = {}, MeshHandle goal = {}, bool separatePlayerVisual = false,
                MeshHandle coin = {}, MeshHandle accent = {}, GravityLevel level = {});
    void Restart();
    void Tick(Controls input, float dt);
    [[nodiscard]] const Scene& World() const noexcept { return *scene_; }
    [[nodiscard]] PhysicsStats Physics() const noexcept { return physics_->Stats(); }
    [[nodiscard]] Vec3 Position() const { return scene_->Get<TransformComponent>(player_)->position; }
    [[nodiscard]] Matrix4 PlayerVisualWorld() const { return scene_->WorldMatrix(separatePlayerVisual_ ? playerVisual_ : player_); }
    [[nodiscard]] State Status() const noexcept { return rules_.Status(); }
    [[nodiscard]] bool Supported() const noexcept { return physics_->Supported(player_); }
    [[nodiscard]] bool Inverted() const noexcept { return inverted_; }
    [[nodiscard]] float Elapsed() const noexcept { return rules_.Elapsed(); }
    [[nodiscard]] unsigned Flips() const noexcept { return rules_.Flips(); }
    [[nodiscard]] unsigned Coins() const noexcept { return rules_.Coins(); }
    [[nodiscard]] static constexpr unsigned TotalCoins() noexcept { return GravityRules::TotalCoins(); }
    [[nodiscard]] MaterialPreset PlayerMaterial() const noexcept { return playerMaterial_; }
    void CyclePlayerMaterial();
private:
    void ApplyPlayerMaterial();
    MeshHandle platformMesh_, playerMesh_, goalMesh_, coinMesh_, accentMesh_;
    bool separatePlayerVisual_ = false;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<PhysicsWorld> physics_;
    EntityId player_;
    EntityId playerVisual_;
    std::array<EntityId,3> coins_{};
    GravityRules rules_;
    bool inverted_ = false;
    float facingYaw_ = 0;
    MaterialPreset playerMaterial_ = MaterialPreset::Silver;
    GravityLevel level_;
};
}
