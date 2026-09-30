#pragma once
#include <remi/physics/PhysicsWorld.hpp>
#include <memory>
#include <array>
namespace remi::game {
enum class State { Playing, Won, Lost };
enum class MaterialPreset { Silver, Original, Gold, Midnight };
struct Controls { float x = 0, z = 0; bool flip = false, restart = false; };
class GravityGame {
public:
    GravityGame(MeshHandle platform = {}, MeshHandle player = {}, MeshHandle goal = {}, bool separatePlayerVisual = false,
                MeshHandle coin = {}, MeshHandle accent = {});
    void Restart();
    void Tick(Controls input, float dt);
    [[nodiscard]] const Scene& World() const noexcept { return *scene_; }
    [[nodiscard]] PhysicsStats Physics() const noexcept { return physics_->Stats(); }
    [[nodiscard]] Vec3 Position() const { return scene_->Get<TransformComponent>(player_)->position; }
    [[nodiscard]] State Status() const noexcept { return state_; }
    [[nodiscard]] bool Supported() const noexcept { return physics_->Supported(player_); }
    [[nodiscard]] bool Inverted() const noexcept { return inverted_; }
    [[nodiscard]] float Elapsed() const noexcept { return elapsed_; }
    [[nodiscard]] unsigned Flips() const noexcept { return flips_; }
    [[nodiscard]] unsigned Coins() const noexcept { return coinsCollected_; }
    [[nodiscard]] static constexpr unsigned TotalCoins() noexcept { return 3; }
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
    std::array<bool,3> coinCollectedFlags_{};
    State state_ = State::Playing;
    bool inverted_ = false;
    float facingYaw_ = 0;
    float elapsed_ = 0;
    unsigned flips_ = 0;
    unsigned coinsCollected_ = 0;
    MaterialPreset playerMaterial_ = MaterialPreset::Silver;
};
}
