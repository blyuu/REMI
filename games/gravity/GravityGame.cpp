#include "GravityGame.hpp"
#include <cmath>
#include <stdexcept>
namespace remi::game {
GravityGame::GravityGame(MeshHandle platform,MeshHandle player,MeshHandle goal,bool separatePlayerVisual,MeshHandle coin,MeshHandle accent)
    : platformMesh_(platform),playerMesh_(player),goalMesh_(goal),coinMesh_(coin),accentMesh_(accent),separatePlayerVisual_(separatePlayerVisual) { Restart(); }
void GravityGame::Restart() {
    auto scene = std::make_unique<Scene>();
    auto physics = std::make_unique<PhysicsWorld>(*scene);
    const auto box = [&](const char* name,Vec3 position,Vec3 half,MeshHandle mesh,bool collision,BodyType type) {
        const auto id = scene->Create(name);
        *scene->Get<TransformComponent>(id) = {position,{},{half.x*2,half.y*2,half.z*2}};
        if (mesh.id) scene->Add<MeshComponent>(id,{mesh,true});
        if (collision) physics->AddBody(id,{type,half});
        return id;
    };
    (void)box("Start platform",{-5,-.25f,0},{3,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Finish platform",{5,-.25f,0},{3,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Ceiling bridge",{0,4.25f,0},{8,.25f,2},platformMesh_,true,BodyType::Static);
    (void)box("Green goal",{6,.06f,0},{.8f,.06f,.8f},goalMesh_,false,BodyType::Static);
    for (unsigned i = 0; i < coins_.size(); ++i) {
        const float x = -3.5f + static_cast<float>(i)*3.5f;
        coins_[i] = box("Gravity coin",{x,3.35f,0},{.35f,.35f,.12f},coinMesh_,false,BodyType::Static);
        if (auto* mesh = scene->Get<MeshComponent>(coins_[i])) mesh->material = {{1.f,.9f,.35f},.7f,.25f,.55f};
        (void)box("Coin marker",{x,3.965f,0},{.65f,.015f,.65f},accentMesh_,false,BodyType::Static);
    }
    (void)box("Ceiling guide left",{0,3.965f,-1.82f},{7.8f,.025f,.035f},accentMesh_,false,BodyType::Static);
    (void)box("Ceiling guide right",{0,3.965f,1.82f},{7.8f,.025f,.035f},accentMesh_,false,BodyType::Static);
    (void)box("Start edge",{-2,.025f,0},{.08f,.025f,1.8f},accentMesh_,false,BodyType::Static);
    (void)box("Finish edge",{2,.025f,0},{.08f,.025f,1.8f},accentMesh_,false,BodyType::Static);
    (void)box("Finish arch left",{6,.8f,-1.15f},{.06f,.8f,.06f},accentMesh_,false,BodyType::Static);
    (void)box("Finish arch right",{6,.8f,1.15f},{.06f,.8f,.06f},accentMesh_,false,BodyType::Static);
    (void)box("Finish arch beam",{6,1.6f,0},{.06f,.06f,1.21f},accentMesh_,false,BodyType::Static);
    const auto player = box("Player",{-6,.4f,0},{.35f,.4f,.35f},separatePlayerVisual_ ? MeshHandle{} : playerMesh_,true,BodyType::Dynamic);
    EntityId visual;
    if (separatePlayerVisual_) {
        visual = scene->Create("Player visual");
        if (playerMesh_.id) scene->Add<MeshComponent>(visual,{playerMesh_,true});
        if (!scene->SetParent(visual,player)) throw std::logic_error("Cannot attach player visual");
    }
    physics->Step(1.f/60); // Establish initial support before the first input tick.
    physics_.reset(); scene_ = std::move(scene); physics_ = std::move(physics); player_ = player; playerVisual_ = visual;
    state_ = State::Playing; inverted_ = false; facingYaw_ = 0; elapsed_ = 0; flips_ = 0; coinsCollected_ = 0;
    coinCollectedFlags_.fill(false);
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
    if (separatePlayerVisual_) {
        auto* visual = scene_->Get<TransformComponent>(playerVisual_);
        if (length > .001f) facingYaw_ = std::atan2(input.x,input.z);
        // The upside-down Z rotation mirrors horizontal facing in this transform order.
        visual->rotation.y = inverted_ ? -facingYaw_ : facingYaw_;
        visual->rotation.z = inverted_ ? 3.14159265f : 0;
    }
    const auto p = Position();
    for (std::size_t i = 0; i < coins_.size(); ++i) {
        if (coinCollectedFlags_[i]) continue;
        const auto coin = coins_[i];
        auto* mesh = scene_->Get<MeshComponent>(coin);
        scene_->Get<TransformComponent>(coin)->rotation.y += dt*2.5f;
        const auto c = scene_->Get<TransformComponent>(coin)->position;
        if (std::abs(p.x-c.x) < .68f && std::abs(p.y-c.y) < .78f && std::abs(p.z-c.z) < .68f) {
            coinCollectedFlags_[i] = true;
            if (mesh) mesh->visible = false;
            ++coinsCollected_;
        }
    }
    if (p.y < -5 || p.y > 10 || std::abs(p.x) > 10 || std::abs(p.z) > 5) state_ = State::Lost;
    else if (!inverted_ && Supported() && coinsCollected_ == TotalCoins() &&
             std::abs(p.x-6) <= .8f && std::abs(p.z) <= .8f && p.y < .5f) state_ = State::Won;
}
}
