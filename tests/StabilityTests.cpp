#include <remi/render/Renderer.hpp>
#include <remi/platform/Window.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2,"Shader path required");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = wc.height = 64;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle();
        config.width = config.height = 64; config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        const std::array<remi::Vertex,4> vertices{{{{-.8f,-.8f,.5f},{1,0,0}},{{-.8f,.8f,.5f},{1,0,0}},{{.8f,-.8f,.5f},{1,0,0}},{{.8f,.8f,.5f},{1,0,0}}}};
        const std::array<std::uint32_t,6> indices{0,1,2,2,1,3};
        auto mesh = renderer.CreateMesh(vertices,indices);
        const auto identity = remi::Matrix4::Identity();
        for (unsigned frame = 0; frame < 128; ++frame) {
            const unsigned width = frame % 2 ? 97 : 64, height = frame % 2 ? 61 : 64;
            renderer.Resize(width,height);
            renderer.BeginShadow(identity); renderer.Draw(*mesh,identity); renderer.EndShadow();
            renderer.Draw(*mesh,identity);
            if (frame % 16 == 0) {
                const auto image = renderer.Readback();
                const auto center = (static_cast<std::size_t>(height/2)*width+width/2)*4;
                Check(image.width == width && image.height == height && image.rgba[center] > 250 && image.rgba[center+1] < 5,
                      "Resize/shadow frame changed pixels");
            }
            renderer.Present();
            Check(renderer.CheckDiagnostics() == 0,"D3D11 warning during resize/shadow cycle");
        }
        mesh.reset();
        const auto audit = renderer.ShutdownAndValidate();
        Check(audit.liveChildren == 0 && audit.priorWarnings == 0,"Stability run leaked D3D objects");
        std::cout << "128 resize and shadow cycles passed; pixels stable; D3D audit clean\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
