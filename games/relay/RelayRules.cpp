#include "RelayRules.hpp"
#include <cmath>

namespace remi::relay {
void RelayRules::Reset(RuleConfig config) noexcept {
    config_ = config;
    status_ = Status::Playing;
    switchActive_ = false;
    elapsed_ = 0;
}

void RelayRules::Tick(Vec3 player, float dt) {
    if (status_ != Status::Playing) return;
    elapsed_ += dt;
    if (player.x < config_.deathMin.x || player.y < config_.deathMin.y || player.z < config_.deathMin.z ||
        player.x > config_.deathMax.x || player.y > config_.deathMax.y || player.z > config_.deathMax.z) {
        status_ = Status::Fell;
        return;
    }
    if (elapsed_ >= config_.timeLimit) { status_ = Status::TimedOut; return; }
    const auto near = [&](Vec3 target) {
        return std::abs(player.x-target.x) < .7f && std::abs(player.z-target.z) < .7f &&
            std::abs(player.y-.4f) < .8f;
    };
    if (near(config_.switchPosition)) switchActive_ = true;
    if (switchActive_ && near(config_.goalPosition)) status_ = Status::Won;
}
}
