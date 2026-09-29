#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace remi {
class FrameClock {
public:
    void Reset() noexcept { previous_ = Clock::now(); }
    [[nodiscard]] double Tick() noexcept {
        const auto now = Clock::now();
        const double seconds = std::chrono::duration<double>(now - previous_).count();
        previous_ = now;
        return seconds;
    }
private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point previous_ = Clock::now();
};

struct StepBatch { unsigned ticks = 0; double alpha = 0; double droppedSeconds = 0; };
class FixedStepper {
public:
    static constexpr double Step = 1.0 / 60.0;
    static constexpr unsigned MaxTicks = 8;
    [[nodiscard]] StepBatch Advance(double elapsed) noexcept {
        if (!std::isfinite(elapsed) || elapsed < 0) elapsed = 0;
        const double accepted = std::min(elapsed, .25);
        StepBatch result{};
        result.droppedSeconds = elapsed - accepted;
        accumulator_ += accepted;
        while (accumulator_ >= Step && result.ticks < MaxTicks) {
            accumulator_ -= Step; ++result.ticks;
        }
        if (accumulator_ >= Step) {
            const double remainder = std::fmod(accumulator_, Step);
            result.droppedSeconds += accumulator_ - remainder;
            accumulator_ = remainder;
        }
        result.alpha = accumulator_ / Step;
        return result;
    }
    void Reset() noexcept { accumulator_ = 0; }
private:
    double accumulator_ = 0;
};
}
