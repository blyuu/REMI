#include "GravityGame.hpp"
#include <cmath>
#include <stdexcept>
namespace remi::game {
GravityGame::GravityGame(MeshHandle platform,MeshHandle player,MeshHandle goal)
    : platformMesh_(platform),playerMesh_(player),goalMesh_(goal) { Restart(); }
void GravityGame::Restart() {
    auto scene = std::make_unique<Scene>();
    auto physics = std::make_unique<PhysicsWorld>(*scene);
    const auto box = [&](const char* name,Vec3 position,Vec3 half,MeshHandle mesh,bool collision,BodyType type) {
        const auto id = scene->Create(name);
        *scene->Get<TransformComponent>(id) = {position,{},{half.x*2,half.y*2,half.z*2}};
        scene->Add<MeshComponent>(id,{mesh,true});
        if (collision) physics->AddBody(id,{type,half});
        return id;
    };
    (void)box("Start platform",{-5,-.25f,0},{3,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Finish platform",{5,-.25f,0},{3,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Ceiling bridge",{0,4.25f,0},{8,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Green goal",{6,.06f,0},{.8f,.06f,.8f},goalMesh_,false,BodyType::Static);
    const auto player = box("Player",{-6,.4f,0},{.35f,.4f,.35f},playerMesh_,true,BodyType::Dynamic);
    physics->Step(1.f/60); // Establish initial support before the first input tick.
    physics_.reset(); scene_ = std::move(scene); physics_ = std::move(physics); player_ = player;
    state_ = State::Playing; inverted_ = false; elapsed_ = 0; flips_ = 0;
}
void GravityGame::Tick(Controls input,float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1.f/30 || !std::isfinite(input.x) || !std::isfinite(input.z) ||
        std::abs(input.x) > 1 || std::abs(input.z) > 1) throw std::invalid_argument("Invalid game tick input");
    if (input.restart) { Restart(); return; }
    if (state_ != State::Playing) return;
    elapsed_ += dt;
    if (input.flip && Supported()) {
        inverted_ = !inverted_; ++flips_;
        physics_->SetGravity({0,inverted_ ? 9.81f : -9.81f,0});
        auto v = physics_->Velocity(player_); v.y = 0; physics_->SetVelocity(player_,v);
    }
    const float length = std::sqrt(input.x*input.x+input.z*input.z);
    const float divisor = length > 1 ? length : 1;
    auto velocity = physics_->Velocity(player_);
    velocity.x = input.x*4/divisor; velocity.z = input.z*4/divisor;
    physics_->SetVelocity(player_,velocity); physics_->Step(dt);
    const auto p = Position();
    if (p.y < -5 || p.y > 10 || std::abs(p.x) > 10 || std::abs(p.z) > 5) state_ = State::Lost;
    else if (!inverted_ && Supported() && std::abs(p.x-6) <= .8f && std::abs(p.z) <= .8f && p.y < .5f) state_ = State::Won;
}
}
