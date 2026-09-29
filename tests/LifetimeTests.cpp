#include <remi/core/Lifetime.hpp>
#include <remi/render/Renderer.hpp>
#include <remi/platform/Window.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "Shader path required");
        const auto baseline = remi::LifetimeToken::Snapshot();
        unsigned validated = 0;
        for (unsigned cycle = 0; cycle < 8; ++cycle) {
            {
                remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = 64; wc.height = 64;
                remi::Window window(wc);
                remi::RendererConfig config; config.nativeWindow = window.NativeHandle();
                config.width = 64; config.height = 64; config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
                const auto windowBaseline = remi::LifetimeToken::Snapshot();
                auto bad = config; bad.shaderFile += L".missing";
                bool rejected = false;
                try { remi::Renderer failure(bad); } catch (const std::exception&) { rejected = true; }
                Check(rejected && remi::LifetimeToken::Snapshot() == windowBaseline, "Partial construction leaked owners");
                remi::Renderer renderer(config);
                const std::array<remi::Vertex,3> vertices{{{{-.5f,-.5f,.5f},{1,0,0}},{{0,.5f,.5f},{0,1,0}},{{.5f,-.5f,.5f},{0,0,1}}}};
                const std::array<std::uint32_t,3> indices{0,1,2};
                auto mesh = renderer.CreateMesh(vertices,indices);
                Check(renderer.LiveMeshes() == 1, "Mesh owner count incorrect");
                rejected = false;
                try { (void)renderer.ShutdownAndValidate(); } catch (const std::logic_error&) { rejected = true; }
                Check(rejected, "Wrong shutdown order accepted");
                renderer.BeginFrame(); renderer.Draw(*mesh,remi::Matrix4::Identity()); renderer.Present();
                mesh.reset();
                const auto report = renderer.ShutdownAndValidate();
                Check(report.liveChildren == 0 && report.priorWarnings == 0, "D3D shutdown audit failed");
                if (report.debugValidated) ++validated;
                Check(renderer.ShutdownAndValidate().debugValidated == report.debugValidated, "Shutdown not idempotent");
                rejected = false;
                try { renderer.BeginFrame(); } catch (const std::logic_error&) { rejected = true; }
                Check(rejected, "Closed renderer accepted frame");
            }
            Check(remi::LifetimeToken::Snapshot() == baseline, "Cycle leaked engine owners");
        }
        std::cout << "8 lifecycle cycles balanced; D3D debug audits: " << validated << "/8\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
