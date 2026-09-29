#include <remi/runtime/Application.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/Time.hpp>
#include <exception>

namespace remi {
int RunApplication(Application& application, const RunConfig& config) {
    try {
        Window window(config.window);
        application.OnStart(window);
        struct StopGuard {
            Application& application;
            ~StopGuard() { application.OnStop(); }
        } stop{application};
        Log("Application started");
        FrameClock clock;
        FixedStepper stepper;
        unsigned frames = 0;
        double dropped = 0;
        while (window.PumpMessages()) {
            unsigned width = 0, height = 0;
            if (window.ConsumeResize(width, height) && width && height) application.OnResize(width, height);
            if (window.ConsumeTimeReset()) { clock.Reset(); stepper.Reset(); }
            if (window.Minimized() || window.Resizing() || (config.pauseWhenInactive && !window.Focused())) {
                window.Inputs().Reset(); clock.Reset(); stepper.Reset();
                window.WaitForEvents(50); continue;
            }
            const double elapsed = clock.Tick();
            const auto batch = stepper.Advance(elapsed);
            dropped += batch.droppedSeconds;
            for (unsigned tick = 0; tick < batch.ticks; ++tick) {
                application.OnFixedUpdate(window, window.Inputs(), FixedStepper::Step);
                window.Inputs().ConsumeTick();
                if (window.Closing()) break;
            }
            if (window.Closing()) break;
            application.OnFrame(window, elapsed, batch.alpha);
            ++frames;
            if (config.frameLimit && frames >= config.frameLimit) window.RequestClose();
            // Event-aware pacing until a renderer supplies Present/VSync in Phase 2.
            if (config.idleWaitMilliseconds) window.WaitForEvents(config.idleWaitMilliseconds);
        }
        Log("Application stopped; dropped simulation seconds: " + std::to_string(dropped));
        return 0;
    } catch (const std::exception& error) {
        Log(std::string("Application failed: ") + error.what());
        return 1;
    } catch (...) {
        Log("Application failed with an unknown exception");
        return 1;
    }
}
}
