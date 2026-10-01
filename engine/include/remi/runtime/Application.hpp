#pragma once
#include <remi/platform/Window.hpp>
#include <remi/runtime/Layer.hpp>
#include <memory>
#include <vector>

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
    void PushLayer(std::unique_ptr<Layer> layer);
    void PushOverlay(std::unique_ptr<Layer> overlay);
    virtual void OnStart(Window&) {}
    virtual void OnResize(unsigned, unsigned) {}
    virtual void OnFixedUpdate(Window&, const Input&, double) {}
    virtual void OnFrame(Window&, double /* elapsed */, double /* interpolationAlpha */) {}
    // Called once after successful OnStart, including when a later callback throws.
    virtual void OnStop() noexcept {}
private:
    friend int RunApplication(Application&, const RunConfig&);
    std::vector<std::unique_ptr<Layer>> layers_;
    std::size_t overlayStart_ = 0;
    bool running_ = false;
};

// Owns Window for the entire callback lifetime; returns nonzero on startup/update failure.
[[nodiscard]] int RunApplication(Application& application, const RunConfig& config = {});
}
