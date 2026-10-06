#pragma once
#include <remi/core/Math.hpp>
#include <array>
#include <cstddef>

namespace remi::game {
enum class State { Playing, Won, Lost };

struct RuleStep {
    std::array<bool,3> collected{};
};
struct GravityRuleConfig {
    std::array<Vec3,3> coinPositions{};
    Vec3 goalPosition{}, goalHalfExtent{};
    Vec3 deathMin{}, deathMax{};
};

// Pure game rules: no Scene, renderer, or PhysicsWorld dependency.
class GravityRules {
public:
    void Reset(GravityRuleConfig config) noexcept;
    void RecordFlip() noexcept { ++flips_; }
    [[nodiscard]] RuleStep Tick(Vec3 player, bool supported, bool inverted, float dt);
    [[nodiscard]] State Status() const noexcept { return state_; }
    [[nodiscard]] float Elapsed() const noexcept { return elapsed_; }
    [[nodiscard]] unsigned Flips() const noexcept { return flips_; }
    [[nodiscard]] unsigned Coins() const noexcept { return coinsCollected_; }
    [[nodiscard]] static constexpr unsigned TotalCoins() noexcept { return 3; }
    [[nodiscard]] bool Collected(std::size_t index) const { return collected_.at(index); }
private:
    GravityRuleConfig config_{};
    std::array<bool,3> collected_{};
    State state_ = State::Playing;
    float elapsed_ = 0;
    unsigned flips_ = 0;
    unsigned coinsCollected_ = 0;
};
}
