#include <remi/core/Input.hpp>
#include <remi/core/Time.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        remi::Input input;
        input.SetKey(remi::Key::Space, true);
        input.SetKey(remi::Key::Space, true);
        input.SetKey(remi::Key::Space, false);
        Check(input.Pressed(remi::Key::Space) && input.Released(remi::Key::Space) && !input.Held(remi::Key::Space), "Quick tap lost");
        remi::FixedStepper timer;
        Check(timer.Advance(.001).ticks == 0 && input.Pressed(remi::Key::Space), "Edge lost before a tick");
        const auto batch = timer.Advance(.05);
        unsigned consumed = 0;
        for (unsigned i = 0; i < batch.ticks; ++i) {
            if (input.Pressed(remi::Key::Space)) ++consumed;
            input.ConsumeTick();
        }
        Check(consumed == 1 && batch.ticks == 3, "Edge duplicated in catch-up ticks");
        input.SetKey(remi::Key::W, true); input.ConsumeTick(); input.SetKey(remi::Key::W, true);
        Check(input.Held(remi::Key::W) && !input.Pressed(remi::Key::W), "Autorepeat became a fresh edge");
        input.MoveMouse(10, 20); input.MoveMouse(-5, 30); input.AddWheel(.5f); input.AddWheel(1);
        Check(input.DeltaX() == -15 && input.DeltaY() == 10 && input.Wheel() == 1.5f, "Mouse accumulation failed");
        input.Reset();
        Check(!input.Held(remi::Key::W) && input.Wheel() == 0 && input.DeltaX() == 0, "Focus reset failed");
        input.SetKey(remi::Key::Count, true);
        Check(!input.Held(remi::Key::Count), "Invalid key accepted");
        timer.Reset();
        const auto stall = timer.Advance(2);
        Check(stall.ticks == remi::FixedStepper::MaxTicks && stall.alpha >= 0 && stall.alpha < 1, "Catch-up not bounded");
        const double accounted = stall.ticks * remi::FixedStepper::Step + stall.alpha * remi::FixedStepper::Step + stall.droppedSeconds;
        Check(std::abs(accounted - 2) < 1e-9, "Dropped time not accounted");
        timer.Reset();
        Check(timer.Advance(-1).ticks == 0, "Negative time accepted");
        Check(timer.Advance(std::numeric_limits<double>::quiet_NaN()).ticks == 0, "NaN time accepted");
        Check(timer.Advance(std::numeric_limits<double>::infinity()).ticks == 0, "Infinite time accepted");
        unsigned total = 0;
        for (unsigned i = 0; i < 120; ++i) total += timer.Advance(1.0 / 120).ticks;
        Check(total == 60, "Fixed rate drift");
        std::cout << "Core input/timing regression passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
