#include <GravityGame.hpp>
#include <GameProject.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
void Check(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        Check(argc == 2,"Project path required");
        using namespace remi::game;
        const auto project = GameProject::Load(argv[1]);
        Check(project.shader.filename() == "Basic.hlsl" && project.font.filename() == "Pretendard-SemiBold.otf" &&
              std::abs(project.level.gravity-9.81f) < .001f && project.level.coinPositions.size() == 3,
              "Project assets or level were not parsed");
        GravityGame game;
        Check(game.Supported() && game.Status() == State::Playing,"Invalid start");
        const auto old = game.World().Entities().front();
        for (unsigned i = 0; i < 200; ++i) game.Tick({1,0},1.f/60);
        Check(game.Status() == State::Lost,"Gap did not cause loss without flip");
        const auto lostPosition = game.Position(); const auto time = game.Elapsed();
        game.Tick({1,0,true},1.f/60);
        Check(game.Position().x == lostPosition.x && game.Elapsed() == time,"Terminal state continued simulation");
        game.Tick({0,0,false,true},1.f/60);
        Check(game.Status() == State::Playing && game.Flips() == 0 && !game.World().Alive(old),"Restart failed");
        game.Tick({0,0,true},1.f/60);
        game.Tick({0,0,true},1.f/60);
        Check(game.Inverted() && game.Flips() == 1,"Airborne flip accepted");
        for (unsigned i = 0; i < 70; ++i) game.Tick({},1.f/60);
        Check(game.Supported() && std::abs(game.Position().y-3.6f) < .001f,"Ceiling landing failed");
        for (unsigned i = 0; i < 180; ++i) game.Tick({1,0},1.f/60);
        Check(game.Status() == State::Playing && game.Coins() == game.TotalCoins(),"Ceiling coins were not collected");
        game.Tick({0,0,true},1.f/60);
        for (unsigned i = 0; i < 70; ++i) game.Tick({},1.f/60);
        Check(game.Status() == State::Won && game.Flips() == 2,"Playable route did not win");
        game.Restart(); game.Tick({0,0,true},1.f/60);
        for (unsigned i = 0; i < 70; ++i) game.Tick({},1.f/60);
        for (unsigned i = 0; i < 16; ++i) game.Tick({0,1},1.f/60);
        for (unsigned i = 0; i < 180; ++i) game.Tick({1,0},1.f/60);
        for (unsigned i = 0; i < 16; ++i) game.Tick({0,-1},1.f/60);
        Check(game.Coins() == 0,"Coins were collected outside the lane");
        game.Tick({0,0,true},1.f/60);
        for (unsigned i = 0; i < 70; ++i) game.Tick({},1.f/60);
        Check(game.Status() == State::Playing,"Goal opened without all coins");
        game.Restart(); const auto start = game.Position(); game.Tick({1,1},1.f/60);
        const auto p = game.Position();
        Check(std::abs(std::hypot(p.x-start.x,p.z-start.z)-4.f/60) < .0001f,"Diagonal movement boosted speed");
        game.Restart();
        for (unsigned i = 0; i < 120; ++i) game.Tick({0,1},1.f/60);
        Check(game.Status() == State::Lost,"Side fall did not end game");
        const auto slotBytes = game.World().Memory().slotCapacityBytes;
        for (unsigned i = 0; i < 1000; ++i) {
            game.Restart(); game.Tick({},1.f/60);
            Check(game.World().Size() == 18 && game.Supported() && game.Coins() == 0,"Restart accumulated entities or coins");
            Check(game.World().Memory().slotCapacityBytes == slotBytes,"Restart grew scene storage");
        }
        GravityGame animated({1,1},{2,1},{3,1},true);
        remi::EntityId visual;
        animated.World().ForEachEntity([&](remi::EntityId id) {
            if (animated.World().Name(id) == "Player visual") visual = id;
        });
        Check(animated.World().Size() == 19 && animated.World().Alive(visual) &&
              animated.World().Name(animated.World().Parent(visual)) == "Player","Animated visual hierarchy missing");
        Check(animated.PlayerMaterial() == MaterialPreset::Silver,"Silver character material was not selected");
        animated.CyclePlayerMaterial();
        Check(animated.PlayerMaterial() == MaterialPreset::Original &&
              animated.World().Get<remi::MeshComponent>(visual)->material.metallic == 0,"Material preset did not update mesh");
        animated.Tick({1,0},1.f/60);
        Check(std::abs(animated.World().Get<remi::TransformComponent>(visual)->rotation.y-1.57079632679f) < .001f,
              "Player visual did not face movement");
        const auto forward = [&] {
            const auto matrix = animated.World().WorldMatrix(visual);
            return remi::Vec3{matrix.values[8],matrix.values[9],matrix.values[10]};
        };
        Check(forward().x > .6f,"Player visual did not face right before flip");
        animated.Tick({0,0,true},1.f/60);
        Check(std::abs(animated.World().Get<remi::TransformComponent>(visual)->rotation.z-3.14159265f) < .001f,
              "Player visual did not follow gravity flip");
        Check(forward().x > .6f,"Player visual faced backward after stationary flip");
        animated.Tick({1,0},1.f/60);
        Check(forward().x > .6f,"Player visual faced backward while inverted and moving right");
        animated.Tick({0,1},1.f/60);
        Check(forward().z > .6f,"Player visual faced backward while inverted and moving forward");
        animated.Tick({-1,0},1.f/60);
        Check(forward().x < -.6f,"Player visual faced backward while inverted and moving left");
        std::cout << "Gap loss, ceiling traversal, two flips, goal win, terminal freeze, diagonal speed, 1000 restarts passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
