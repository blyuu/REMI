#include <remi/debug/SettingsMenu.hpp>
#include <remi/platform/Window.hpp>
#include <stdexcept>
#include <filesystem>
#include <iostream>

void Check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }

int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc == 3 || argc == 4,"Shader, font, and optional capture path required");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true;
        wc.width = 800; wc.height = 600;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle();
        config.width = wc.width; config.height = wc.height;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        {
            remi::ui::SettingsMenu menu(argv[2],L"REMI SETTINGS",
                {L"WASD   이동",L"마우스   카메라 회전"});
            const auto draw = [&] {
                renderer.BeginFrame({.16f,.21f,.23f});
                menu.Update(renderer,wc.width,wc.height);
                menu.Draw(renderer);
                auto frame = renderer.Readback();
                if (argc == 4 && menu.Open()) renderer.SaveScreenshot(argv[3]);
                renderer.Present();
                return frame;
            };
            const auto closed = draw();
            remi::Input input;
            input.MoveMouse(static_cast<int>(wc.width)-80,35);
            input.SetKey(remi::Key::MouseLeft,true);
            Check(menu.HandleClick(input,wc.width,wc.height) && menu.Open(),"Gear click did not open settings");
            const auto opened = draw();
            Check(closed.rgba != opened.rgba,"Settings panel did not render");
            input.ConsumeTick(); input.SetKey(remi::Key::MouseLeft,false); input.ConsumeTick();
            input.SetKey(remi::Key::MouseLeft,true);
            Check(menu.HandleClick(input,wc.width,wc.height) && !menu.Open(),"Gear click did not close settings");
            menu.Clear();
        }
        const auto report = renderer.ShutdownAndValidate();
        Check(report.liveChildren == 0 && report.priorWarnings == 0,"Settings UI leaked D3D objects");
        std::cout << "Noto Sans KR settings icon, click toggle, render, and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
