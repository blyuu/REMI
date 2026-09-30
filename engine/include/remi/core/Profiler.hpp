#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <stdexcept>
namespace remi {
enum class CpuStage : std::size_t { Physics, Shadow, Color, Present, Count };
struct CpuProfile {
    std::array<double,static_cast<std::size_t>(CpuStage::Count)> milliseconds{};
    double frameMs = 0, fps = 0;
    unsigned frames = 0;
};
class CpuProfiler {
public:
    using Clock = std::chrono::steady_clock;
    void BeginFrame() noexcept { current_ = {}; }
    void Add(CpuStage stage,Clock::time_point start) noexcept {
        current_[static_cast<std::size_t>(stage)] += std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
    void Record(CpuStage stage,double milliseconds) noexcept { current_[static_cast<std::size_t>(stage)] += milliseconds; }
    void EndFrame(double frameSeconds) noexcept {
        const double elapsed = frameSeconds*1000;
        // Ignore the tiny initial Runtime delta; it is not a representative frame.
        if (elapsed < .05 || elapsed > 1000) return;
        const double weight = samples_ < 30 ? 1.0/static_cast<double>(samples_+1) : .08;
        for (std::size_t i = 0; i < current_.size(); ++i)
            smoothed_.milliseconds[i] += (current_[i]-smoothed_.milliseconds[i])*weight;
        smoothed_.frameMs += (elapsed-smoothed_.frameMs)*weight;
        smoothed_.fps = smoothed_.frameMs > 0 ? 1000.0/smoothed_.frameMs : 0;
        if (samples_ < 30) ++samples_;
        ++smoothed_.frames;
    }
    [[nodiscard]] CpuProfile Snapshot() const noexcept { return smoothed_; }
private:
    std::array<double,static_cast<std::size_t>(CpuStage::Count)> current_{};
    CpuProfile smoothed_{};
    unsigned samples_ = 0;
};
}
