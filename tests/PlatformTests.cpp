#include <remi/runtime/Application.hpp>
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <vector>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Probe final : remi::Application {
    bool fail = false, failStart = false;
    unsigned frames = 0, stops = 0, resizes = 0;
    void OnStart(remi::Window&) override { if (failStart) throw std::runtime_error("Expected startup failure"); }
    void OnResize(unsigned width, unsigned height) override { Check(width > 0 && height > 0, "Zero resize delivered"); ++resizes; }
    void OnFrame(remi::Window&, double, double alpha) override {
        Check(alpha >= 0 && alpha < 1, "Invalid render interpolation"); ++frames;
        if (fail) throw std::runtime_error("Expected frame failure");
    }
    void OnStop() noexcept override { ++stops; }
};
struct TraceLayer final : remi::Layer {
    std::vector<int>& trace; int id; bool fail = false;
    TraceLayer(std::vector<int>& trace, int id) : trace(trace), id(id) {}
    void OnAttach(remi::Window&) override { trace.push_back(id); }
    void OnFrame(remi::Window&, double, double) override { trace.push_back(id * 10); if (fail) throw std::runtime_error("Layer failure"); }
    void OnDetach() noexcept override { trace.push_back(-id); }
};
int main() {
    try {
        HWND previous = nullptr;
        for (int i = 0; i < 8; ++i) {
            Check(!previous || !IsWindow(previous), "Previous window leaked");
            remi::WindowConfig config; config.visible = false; config.width = 640; config.height = 360;
            remi::Window window(config);
            const auto hwnd = static_cast<HWND>(window.NativeHandle());
            previous = hwnd;
            Check(IsWindow(hwnd) != FALSE && window.Width() == 640 && window.Height() == 360, "Window creation/client size failed");
            SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
            SendMessageW(hwnd, WM_KEYDOWN, 'W', 0);
            Check(window.Inputs().Held(remi::Key::W), "Key mapping failed");
            window.Inputs().ConsumeTick();
            SendMessageW(hwnd, WM_KEYDOWN, 'W', static_cast<LPARAM>(1ULL << 30));
            Check(!window.Inputs().Pressed(remi::Key::W), "Repeat generated edge");
            SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
            Check(!window.Focused() && !window.Inputs().Held(remi::Key::W), "Focus-loss sticky key");
            SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(15, 12));
            SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            Check(window.Inputs().DeltaX() == 5 && window.Inputs().DeltaY() == -8 && window.Inputs().Wheel() == 1, "Mouse translation failed");
            RECT rect{0, 0, 800, 450};
            Check(AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0) != FALSE, "AdjustWindowRect failed");
            Check(SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Resize failed");
            unsigned width = 0, height = 0;
            Check(window.ConsumeResize(width, height) && width == 800 && height == 450, "Resize not propagated");
            Check(!window.ConsumeResize(width, height), "Resize repeated");
            SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
            Check(window.Minimized() && window.Width() == 0, "Minimize state failed");
            SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(800, 450));
            Check(!window.Minimized() && window.ConsumeTimeReset(), "Resume did not reset time");
            SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
            Check(window.Resizing(), "Resize modal state missing");
            SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
            Check(!window.Resizing() && window.ConsumeTimeReset(), "Resize modal exit missing");
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            Check(!window.PumpMessages(), "WM_CLOSE did not stop loop");
        }
        Check(!IsWindow(previous), "Final window leaked");
        remi::RunConfig config; config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 3;
        Probe success;
        Check(remi::RunApplication(success, config) == 0 && success.frames == 3 && success.stops == 1 && success.resizes == 1, "Runtime lifecycle failed");
        std::vector<int> trace;
        remi::Application layered;
        layered.PushOverlay(std::make_unique<TraceLayer>(trace,3));
        layered.PushLayer(std::make_unique<TraceLayer>(trace,1));
        layered.PushLayer(std::make_unique<TraceLayer>(trace,2));
        config.frameLimit = 1;
        Check(remi::RunApplication(layered,config) == 0 &&
              trace == std::vector<int>({1,2,3,10,20,30,-3,-2,-1}), "Layer/overlay order or cleanup failed");
        trace.clear();
        remi::Application failedLayer;
        auto bad = std::make_unique<TraceLayer>(trace,2); bad->fail = true;
        failedLayer.PushLayer(std::make_unique<TraceLayer>(trace,1)); failedLayer.PushLayer(std::move(bad));
        Check(remi::RunApplication(failedLayer,config) == 1 &&
              trace == std::vector<int>({1,2,10,20,-2,-1}), "Failed layer skipped reverse cleanup");
        config.frameLimit = 3;
        Probe failure; failure.fail = true;
        Check(remi::RunApplication(failure, config) == 1 && failure.stops == 1, "Failed update skipped cleanup");
        Probe startup; startup.failStart = true;
        Check(remi::RunApplication(startup, config) == 1 && startup.stops == 0, "Startup failure contract violated");
        config.window.width = 0;
        Probe invalid;
        Check(remi::RunApplication(invalid, config) == 1 && invalid.stops == 0, "Invalid size accepted");
        std::cout << "Win32 and runtime regression passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
