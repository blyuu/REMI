#pragma once
#include <remi/physics/PhysicsWorld.hpp>
#include <memory>
namespace remi::game {
enum class State { Playing, Won, Lost };
struct Controls { float x = 0, z = 0; bool flip = false, restart = false; };
class GravityGame {
public:
    GravityGame(MeshHandle platform = {}, MeshHandle player = {}, MeshHandle goal = {});
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
private:
    MeshHandle platformMesh_, playerMesh_, goalMesh_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<PhysicsWorld> physics_;
    EntityId player_;
    State state_ = State::Playing;
    bool inverted_ = false;
    float elapsed_ = 0;
    unsigned flips_ = 0;
};
}
