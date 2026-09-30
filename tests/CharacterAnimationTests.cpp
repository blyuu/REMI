#include <remi/render/BakedAnimation.hpp>
#include <remi/platform/Window.hpp>
#include <iostream>
#include <stdexcept>

void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc == 3 || argc == 5 || argc == 7,"Shader, baked character and optional capture paths required");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = wc.height = 256;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = config.height = 256;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        auto animation = remi::BakedAnimation::Load(argv[2]);
        Check(animation->IdleFrames() > 1 && animation->RunFrames() > 1,"Character clips missing");
        auto mesh = animation->CreateMesh(renderer);
        remi::Camera camera; camera.distance = 3; camera.target = {0,.25f,0};
        const auto vp = camera.ViewProjection(1);
        remi::DirectionalLight light; light.ambient = .38f; light.cameraPosition = camera.Position();
        const auto world = remi::Matrix4::Identity();
        renderer.BeginFrame({0,0,0}); renderer.DrawLit(*mesh,world,vp,light);
        if (argc >= 5) renderer.SaveScreenshot(argv[3]);
        const auto idle = renderer.Readback(); renderer.Present();
        animation->Update(renderer,*mesh,true,.5);
        renderer.BeginFrame({0,0,0}); renderer.DrawLit(*mesh,world,vp,light);
        if (argc >= 5) renderer.SaveScreenshot(argv[4]);
        const auto running = renderer.Readback(); renderer.Present();
        if (argc == 7) {
            renderer.BeginFrame({0,0,0});
            renderer.DrawLit(*mesh,world,vp,light,{{.72f,.78f,.86f},.82f,.24f});
            renderer.SaveScreenshot(argv[5]);
            const auto silver = renderer.Readback(); renderer.Present();
            renderer.BeginFrame({0,0,0});
            renderer.DrawLit(*mesh,world,vp,light,{{.9f,.61f,.2f},.78f,.27f});
            renderer.SaveScreenshot(argv[6]);
            const auto gold = renderer.Readback(); renderer.Present();
            Check(silver.rgba != gold.rgba,"Character material presets rendered identical pixels");
        }
        std::size_t changed = 0, visible = 0;
        for (std::size_t i = 0; i < running.rgba.size(); i += 4) {
            changed += idle.rgba[i] != running.rgba[i] || idle.rgba[i+1] != running.rgba[i+1] || idle.rgba[i+2] != running.rgba[i+2];
            visible += running.rgba[i] || running.rgba[i+1] || running.rgba[i+2];
        }
        Check(visible > 100 && changed > 100,"Run clip did not change visible character pose");
        bool rejected = false;
        try { animation->Update(renderer,*mesh,false,-1); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected,"Invalid animation elapsed time accepted");
        Check(renderer.CheckDiagnostics() == 0,"D3D warnings during animation");
        mesh.reset(); const auto audit = renderer.ShutdownAndValidate();
        Check(audit.liveChildren == 0 && audit.priorWarnings == 0,"Animated mesh leaked D3D objects");
        std::cout << "Idle/run visible pixel delta=" << changed << ", run pixels=" << visible << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
