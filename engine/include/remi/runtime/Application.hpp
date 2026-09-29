#pragma once
#include <remi/platform/Window.hpp>

namespace remi {
struct RunConfig {
    WindowConfig window;
    bool pauseWhenInactive = true;
    unsigned frameLimit = 0; // zero = interactive; bounded runs for smoke tests
    unsigned idleWaitMilliseconds = 8; // 0 when renderer Present already paces frames
};

class Application {
public:
    virtual ~Application() = default;
    virtual void OnStart(Window&) {}
    virtual void OnResize(unsigned, unsigned) {}
    virtual void OnFixedUpdate(Window&, const Input&, double) {}
    virtual void OnFrame(Window&, double /* elapsed */, double /* interpolationAlpha */) {}
    // Called once after successful OnStart, including when a later callback throws.
    virtual void OnStop() noexcept {}
};

// Owns Window for the entire callback lifetime; returns nonzero on startup/update failure.
[[nodiscard]] int RunApplication(Application& application, const RunConfig& config = {});
}
