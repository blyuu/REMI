#include "GravityRules.hpp"
#include <cmath>

namespace remi::game {
void GravityRules::Reset(GravityRuleConfig config) noexcept {
    config_ = config;
    collected_.fill(false);
    state_ = State::Playing;
    elapsed_ = 0;
    flips_ = 0;
    coinsCollected_ = 0;
}

RuleStep GravityRules::Tick(Vec3 player, bool supported, bool inverted, float dt) {
    RuleStep step;
    if (state_ != State::Playing) return step;
    elapsed_ += dt;
    for (std::size_t i = 0; i < collected_.size(); ++i) {
        if (collected_[i]) continue;
        const auto coin = config_.coinPositions[i];
        if (std::abs(player.x-coin.x) < .68f && std::abs(player.y-coin.y) < .78f &&
            std::abs(player.z-coin.z) < .68f) {
            collected_[i] = true;
            step.collected[i] = true;
            ++coinsCollected_;
        }
    }
    if (player.x < config_.deathMin.x || player.y < config_.deathMin.y || player.z < config_.deathMin.z ||
        player.x > config_.deathMax.x || player.y > config_.deathMax.y || player.z > config_.deathMax.z)
        state_ = State::Lost;
    else if (!inverted && supported && coinsCollected_ == TotalCoins() &&
        std::abs(player.x-config_.goalPosition.x) <= config_.goalHalfExtent.x &&
        std::abs(player.z-config_.goalPosition.z) <= config_.goalHalfExtent.z &&
        player.y < config_.goalPosition.y+.44f)
        state_ = State::Won;
    return step;
}
}
