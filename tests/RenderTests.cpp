#include <remi/render/Renderer.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/platform/Window.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::unique_ptr<remi::Mesh> Quad(remi::Renderer& renderer, float z, remi::Vec3 color) {
    const std::array<remi::Vertex, 4> vertices{{{{-.8f,-.8f,z},color},{{-.8f,.8f,z},color},{{.8f,-.8f,z},color},{{.8f,.8f,z},color}}};
    const std::array<std::uint32_t, 6> indices{0,1,2,2,1,3};
    return renderer.CreateMesh(vertices, indices);
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 2, "Shader path required");
        remi::Camera camera;
        const auto vp = camera.ViewProjection(16.f / 9);
        const auto identity = remi::Multiply(remi::Matrix4::Identity(), vp);
        for (unsigned i = 0; i < 16; ++i) Check(std::abs(identity.values[i] - vp.values[i]) < 1e-6f, "Matrix layout mismatch");
        const auto eye = camera.Position();
        const auto project = [&](remi::Vec3 point, unsigned column) {
            return point.x * vp.values[column] + point.y * vp.values[4 + column] + point.z * vp.values[8 + column] + vp.values[12 + column];
        };
        Check(std::abs(project(camera.target, 0)) < 1e-5f && std::abs(project(camera.target, 1)) < 1e-5f, "Camera target not centered");
        Check(project(camera.target, 3) > 0 && std::abs(project(eye, 3)) < 1e-5f, "View direction incorrect");
        bool rejected = false;
        try { (void)camera.ViewProjection(0); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Invalid aspect accepted");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = 128; wc.height = 96;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = 128; config.height = 96;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        auto nearQuad = Quad(renderer, .2f, {1,0,0}); auto farQuad = Quad(renderer, .8f, {0,1,0});
        for (unsigned pass = 0; pass < 6; ++pass) {
            if (pass == 2) renderer.Resize(97,61);
            if (pass == 4) { renderer.Resize(0,0); Check(renderer.Width() == 97, "Minimize recreated targets"); renderer.Resize(128,96); }
            renderer.BeginFrame({0,0,0});
            if (pass % 2 == 0) { renderer.Draw(*nearQuad, remi::Matrix4::Identity()); renderer.Draw(*farQuad, remi::Matrix4::Identity()); }
            else { renderer.Draw(*farQuad, remi::Matrix4::Identity()); renderer.Draw(*nearQuad, remi::Matrix4::Identity()); }
            const auto image = renderer.Readback();
            const auto center = (static_cast<std::size_t>(image.height / 2) * image.width + image.width / 2) * 4;
            Check(image.width == renderer.Width() && image.height == renderer.Height(), "Readback size mismatch");
            Check(image.rgba[center] > 250 && image.rgba[center+1] < 5 && image.rgba[center+2] < 5, "Depth test or winding failed");
            Check(image.rgba[0] == 0 && image.rgba[1] == 0 && image.rgba[2] == 0, "Clear or viewport incorrect");
            renderer.Present();
        }
        auto gray = Quad(renderer, .5f, {.25f,.25f,.25f});
        renderer.BeginFrame(); renderer.Draw(*gray, remi::Matrix4::Identity());
        const auto image = renderer.Readback();
        const auto center = (static_cast<std::size_t>(image.height/2)*image.width + image.width/2)*4;
        Check(image.rgba[center] >= 135 && image.rgba[center] <= 139, "Linear to sRGB output incorrect");
        renderer.Present();
        renderer.BeginFrame({0,0,0});
        renderer.Draw(*farQuad, remi::Matrix4::Identity());
        renderer.Draw(*nearQuad, remi::ModelMatrix({.9f,0,0},0,{1,1,1}));
        const auto shifted = renderer.Readback();
        Check(shifted.rgba[center] < 5 && shifted.rgba[center+1] > 250, "GPU matrix translation failed");
        renderer.Present();
        auto clipped = Quad(renderer, -.1f, {1,0,0});
        renderer.BeginFrame({0,0,0}); renderer.Draw(*clipped, remi::Matrix4::Identity());
        Check(renderer.Readback().rgba[center] == 0, "Near clip failed"); renderer.Present();
        const std::array<remi::Vertex,1> invalidVertices{{{{0,0,0},{1,1,1}}}};
        const std::array<std::uint32_t,3> invalidIndices{0,1,2};
        rejected = false;
        try { (void)renderer.CreateMesh(invalidVertices,invalidIndices); } catch(const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Out-of-range index accepted");
        {
            remi::Scene scene;
            const auto parent = scene.Create("parent");
            scene.Get<remi::TransformComponent>(parent)->position.x = .9f;
            const auto child = scene.Create("child");
            scene.Add<remi::MeshComponent>(child,{{1,1},true});
            Check(scene.SetParent(child,parent), "Scene parent failed");
            const auto hidden = scene.Create("hidden"); scene.Add<remi::MeshComponent>(hidden,{{1,1},false});
            const auto missing = scene.Create("missing"); scene.Add<remi::MeshComponent>(missing,{{99,1},true});
            renderer.BeginFrame({0,0,0});
            const auto stats = remi::DrawScene(renderer,scene,remi::Matrix4::Identity(),[&](remi::MeshHandle key) {
                return key == remi::MeshHandle{1,1} ? nearQuad.get() : nullptr;
            });
            Check(stats.draws == 1 && stats.hidden == 1 && stats.missing == 1, "Scene draw filtering failed");
            const auto pixels = renderer.Readback();
            Check(pixels.rgba[center] == 0, "Scene parent transform not applied");
            const auto right = (static_cast<std::size_t>(pixels.height/2)*pixels.width + pixels.width*3/4)*4;
            Check(pixels.rgba[right] > 250, "Scene mesh not drawn at inherited position");
            renderer.Present();
        }
        {
            remi::Window another(wc);
            auto broken = config; broken.nativeWindow = another.NativeHandle(); broken.shaderFile += L".missing";
            rejected = false;
            try { remi::Renderer bad(broken); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected, "Missing shader did not report startup failure");
        }
        {
            const auto temp = std::filesystem::temp_directory_path() / "remi_renderer_reload_test.hlsl";
            std::filesystem::copy_file(argv[1],temp,std::filesystem::copy_options::overwrite_existing);
            remi::Window another(wc);
            auto reloadConfig = config; reloadConfig.nativeWindow = another.NativeHandle(); reloadConfig.shaderFile = temp;
            remi::Renderer hot(reloadConfig);
            { std::ofstream out(temp,std::ios::app); out << "\n// live reload\n"; }
            Check(hot.ReloadShaders(),"Renderer did not rebuild shaders after file change");
            {
                std::ifstream input(argv[1],std::ios::binary);
                std::string source{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
                const std::string inputNormal = "float3 normal : NORMAL;";
                const std::string transformNormal = "output.normal = mul(input.normal,(float3x3)g_World);";
                const auto remove = source.find(inputNormal);
                const auto replace = source.find(transformNormal);
                Check(remove != std::string::npos && replace != std::string::npos,"Shader signature test source changed");
                source.replace(replace,transformNormal.size(),"output.normal = float3(0,0,1);");
                source.erase(remove,inputNormal.size());
                std::ofstream out(temp,std::ios::binary|std::ios::trunc); out << source;
            }
            Check(hot.ReloadShaders(),"Changed vertex input signature was not reflected into layout");
            { std::ofstream out(temp,std::ios::trunc); out << "invalid HLSL"; }
            rejected = false;
            try { (void)hot.ReloadShaders(); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected,"Invalid shader edit was accepted");
            auto triangle = Quad(hot,.5f,{1,0,0});
            hot.BeginFrame({0,0,0}); hot.Draw(*triangle,remi::Matrix4::Identity());
            const auto frame = hot.Readback(); hot.Present();
            Check(frame.rgba[center] > 250,"Renderer lost previous shader after failed reload");
            triangle.reset();
            const auto report = hot.ShutdownAndValidate();
            Check(report.liveChildren == 0 && report.priorWarnings == 0,"Shader reload leaked D3D objects");
            std::filesystem::remove(temp);
        }
        Check(renderer.CheckDiagnostics() == 0, "D3D11 debug warning/error detected");
        std::cout << "GPU readback: depth/order, resize, viewport, sRGB and camera passed. Debug layer: "
                  << (renderer.DebugLayerEnabled() ? "enabled" : "unavailable; validation not performed") << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
