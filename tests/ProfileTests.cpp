#include <remi/debug/DebugOverlay.hpp>
#include <remi/core/Profiler.hpp>
#include <remi/platform/Window.hpp>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc == 2,"Shader path required");
        remi::CpuProfiler cpu;
        cpu.BeginFrame(); cpu.Record(remi::CpuStage::Physics,2); cpu.EndFrame(.0000001);
        Check(cpu.Snapshot().frames == 0 && cpu.Snapshot().fps == 0,"Tiny first frame skewed FPS");
        cpu.BeginFrame(); cpu.Record(remi::CpuStage::Physics,2); cpu.EndFrame(.016);
        Check(cpu.Snapshot().frames == 1 && cpu.Snapshot().fps > 62 && cpu.Snapshot().fps < 63 && cpu.Snapshot().milliseconds[0] == 2,"CPU profile incorrect");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = 320; wc.height = 160;
        remi::Window window(wc);
        remi::RendererConfig rc; rc.nativeWindow = window.NativeHandle(); rc.width = 320; rc.height = 160;
        rc.shaderFile = argv[1]; rc.useWarp = true; rc.vsync = false;
        remi::Renderer renderer(rc); remi::debug::DebugOverlay overlay;
        const std::array<std::string,2> lines{"REMI DEBUG F2", "FPS 62.50 CPU 2.00"};
        overlay.Update(renderer,320,160,lines);
        bool gpuReady = false;
        for (unsigned i = 0; i < 12; ++i) {
            renderer.BeginGpuProfile(); renderer.BeginShadow(remi::Matrix4::Identity()); renderer.EndShadow();
            overlay.Draw(renderer); renderer.EndGpuProfile();
            const auto pixels = renderer.Readback();
            const auto at = [&](unsigned x,unsigned y,unsigned c) { return pixels.rgba[(y*pixels.width+x)*4+c]; };
            Check(at(24,16,1) > at(40,18,1)+80,"Overlay text not visible above panel");
            Check(at(9,13,1) > at(40,13,1)+80,"Overlay accent missing");
            renderer.Present();
            const auto sample = renderer.PollGpuProfile();
            if (sample.valid) { Check(sample.totalMs >= sample.shadowMs && sample.totalMs >= sample.colorMs,"GPU timestamps invalid"); gpuReady = true; }
        }
        Check(gpuReady,"GPU query never produced a sample");
        Check(renderer.CheckDiagnostics() == 0,"Debug layer warnings");
        overlay.Clear(); const auto audit = renderer.ShutdownAndValidate();
        Check(audit.liveChildren == 0 && audit.priorWarnings == 0,"Profiler or overlay leaked GPU resources");
        std::cout << "Visible text, CPU FPS guard, asynchronous GPU timestamps and clean shutdown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
