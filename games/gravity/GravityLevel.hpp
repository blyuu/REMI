#pragma once
#include <remi/physics/PhysicsWorld.hpp>
#include <array>

namespace remi::game {
struct LevelBox { Vec3 position; Vec3 halfExtent; };
struct GravityLevel {
    float gravity = 9.81f;
    float moveSpeed = 4.f;
    PhysicsSettings physics{};
    Vec3 playerSpawn{-6,.4f,0};
    std::array<LevelBox,3> platforms{{{{-5,-.25f,0},{3,.25f,2}}, {{5,-.25f,0},{3,.25f,2}}, {{0,4.25f,0},{8,.25f,2}}}};
    Vec3 goalPosition{6,.06f,0};
    Vec3 goalHalfExtent{.8f,.06f,.8f};
    std::array<Vec3,3> coinPositions{{{-3.5f,3.35f,0},{0,3.35f,0},{3.5f,3.35f,0}}};
    Vec3 deathMin{-10,-5,-5}, deathMax{10,10,5};
};
}
