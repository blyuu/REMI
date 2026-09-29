#include <GravityGame.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
void Check(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
int main() {
    try {
        using namespace remi::game;
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
        Check(game.Status() == State::Playing,"Goal triggered at ceiling height");
        game.Tick({0,0,true},1.f/60);
        for (unsigned i = 0; i < 70; ++i) game.Tick({},1.f/60);
        Check(game.Status() == State::Won && game.Flips() == 2,"Playable route did not win");
        game.Restart(); const auto start = game.Position(); game.Tick({1,1},1.f/60);
        const auto p = game.Position();
        Check(std::abs(std::hypot(p.x-start.x,p.z-start.z)-4.f/60) < .0001f,"Diagonal movement boosted speed");
        game.Restart();
        for (unsigned i = 0; i < 120; ++i) game.Tick({0,1},1.f/60);
        Check(game.Status() == State::Lost,"Side fall did not end game");
        for (unsigned i = 0; i < 100; ++i) { game.Restart(); Check(game.World().Size() == 5 && game.Supported(),"Restart accumulated entities"); }
        std::cout << "Gap loss, ceiling traversal, two flips, goal win, terminal freeze, diagonal speed, 100 restarts passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
