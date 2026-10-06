#pragma once
#include <remi/core/Math.hpp>

namespace remi::relay {
enum class Status { Playing, Won, TimedOut, Fell };
struct RuleConfig {
    Vec3 switchPosition{0,.1f,1.2f};
    Vec3 goalPosition{6,.1f,0};
    Vec3 deathMin{-9,-2,-4}, deathMax{9,5,4};
    float timeLimit = 12;
};

// A different game's win condition, independent of Scene and PhysicsWorld.
class RelayRules {
public:
    void Reset(RuleConfig config) noexcept;
    void Tick(Vec3 player, float dt);
    [[nodiscard]] Status Result() const noexcept { return status_; }
    [[nodiscard]] bool SwitchActive() const noexcept { return switchActive_; }
    [[nodiscard]] float Remaining() const noexcept { return static_cast<float>(config_.timeLimit-elapsed_); }
private:
    RuleConfig config_{};
    Status status_ = Status::Playing;
    bool switchActive_ = false;
    double elapsed_ = 0;
};
}
