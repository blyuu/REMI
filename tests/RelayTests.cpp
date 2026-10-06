#include <RelayGame.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main() {
    try {
        using namespace remi::relay;
        RelayGame game;
        Check(game.World().Size() == 6 && game.Result() == Status::Playing,
            "Relay did not build an independent scene");
        for (unsigned i = 0; i < 18; ++i) game.Tick({0,1},1.f/60);
        for (unsigned i = 0; i < 90; ++i) game.Tick({1,0},1.f/60);
        Check(game.SwitchActive() && game.Result() == Status::Playing,
            "Relay switch was not activated");
        for (unsigned i = 0; i < 18; ++i) game.Tick({0,-1},1.f/60);
        for (unsigned i = 0; i < 90; ++i) game.Tick({1,0},1.f/60);
        Check(game.Result() == Status::Won && game.Remaining() > 0,
            "Relay route did not finish before timeout");
        const auto wonPosition = game.Position();
        game.Tick({1,0},1.f/60);
        Check(game.Position().x == wonPosition.x,"Won game kept moving");
        game.Restart();
        Check(!game.SwitchActive() && game.Result() == Status::Playing &&
            std::abs(game.Position().x+6) < .001f,"Relay restart retained game state");
        for (unsigned i = 0; i < 12*60; ++i) game.Tick({},1.f/60);
        Check(game.Result() == Status::TimedOut,"Relay time limit did not expire");
        game.Tick({0,0,true},1.f/60);
        Check(game.Result() == Status::Playing && game.Physics().bodies == 4,
            "Relay restart did not rebuild physics bodies");
        RelayRules rules; RuleConfig config; rules.Reset(config);
        rules.Tick(config.goalPosition,1.f/60);
        Check(rules.Result() == Status::Playing && !rules.SwitchActive(),
            "Relay goal opened without switch");
        rules.Tick(config.switchPosition,1.f/60);
        rules.Tick(config.goalPosition,1.f/60);
        Check(rules.Result() == Status::Won,"Independent relay rules did not award win");
        std::cout << "Independent relay route, switch rule, timeout, and restart passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
