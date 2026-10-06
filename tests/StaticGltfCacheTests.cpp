#include <remi/assets/StaticGltfCache.hpp>
#include <remi/platform/Window.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("remi-static-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { Check(std::filesystem::create_directory(root),"Fixture creation failed"); }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove(root/"model.glb",ignored);
        std::filesystem::remove(root/"sample.png",ignored);
        std::filesystem::remove(root,ignored);
    }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 3,"Shader and fixtures required");
        Fixture fixture;
        const std::filesystem::path source(argv[2]);
        const auto file = fixture.root/"model.glb";
        std::filesystem::copy_file(source/"static_triangle.glb",file);
        std::filesystem::copy_file(source/"sample.png",fixture.root/"sample.png");
        remi::WindowConfig wc; wc.visible = false; wc.graphicsSurface = true; wc.width = wc.height = 64;
        remi::Window window(wc);
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = config.height = 64;
        config.shaderFile = argv[1]; config.useWarp = true; config.vsync = false;
        remi::Renderer renderer(config);
        remi::ResourceCache<remi::Mesh> meshes;
        {
            remi::assets::StaticGltfCache assets(renderer,meshes);
            remi::Scene first, second;
            const auto a = assets.Instantiate(first,file);
            const auto b = assets.Instantiate(second,fixture.root/"."/"model.glb");
            const auto handle = first.Get<remi::MeshComponent>(a.at(0))->mesh;
            Check(handle == second.Get<remi::MeshComponent>(b.at(0))->mesh &&
                  renderer.LiveMeshes() == 1 && meshes.Size() == 1 && assets.Size() == 1,
                  "Two scenes duplicated a static model and its texture");
            const auto draw = [&] {
                renderer.BeginFrame({0,0,0});
                const auto stats = remi::DrawScene(renderer,second,remi::Matrix4::Identity(),
                    [&](remi::MeshHandle id) { return meshes.Get(id); },nullptr,false);
                Check(stats.draws == 1,"Cached instance no longer draws");
                auto image = renderer.Readback(); renderer.Present(); return image;
            };
            const auto before = draw();
            { std::ofstream corrupt(file,std::ios::binary|std::ios::trunc); corrupt << "invalid"; }
            bool failed = false;
            try { assets.Reload(file); } catch (const std::exception&) { failed = true; }
            Check(failed && meshes.Get(handle) && renderer.LiveMeshes() == 1 && draw().rgba == before.rgba,
                  "Failed reload lost the previous GPU asset");
            std::filesystem::copy_file(source/"static_triangle.glb",file,std::filesystem::copy_options::overwrite_existing);
            { std::ofstream corrupt(fixture.root/"sample.png",std::ios::binary|std::ios::trunc); corrupt << "invalid"; }
            failed = false;
            try { assets.Reload(file); } catch (const std::exception&) { failed = true; }
            Check(failed && renderer.LiveMeshes() == 1 && draw().rgba == before.rgba,
                  "Failure after GPU mesh creation leaked or replaced the live asset");
            std::filesystem::copy_file(source/"sample.png",fixture.root/"sample.png",std::filesystem::copy_options::overwrite_existing);
            assets.Reload(file);
            Check(second.Get<remi::MeshComponent>(b[0])->mesh == handle && renderer.LiveMeshes() == 1 &&
                  draw().rgba == before.rgba,"Successful reload broke existing scene instances");
            first.Clear();
            Check(meshes.Get(handle) && draw().rgba == before.rgba,"Unloading one scene destroyed a shared model");
            Check(assets.Unload(file) && !meshes.Get(handle) && meshes.Size() == 0,
                  "Explicit model unload did not invalidate handles");
            second.Clear();
            (void)assets.Instantiate(first,file);
        }
        Check(meshes.Size() == 0 && renderer.LiveMeshes() == 0,"Model cache destruction leaked meshes");
        const auto report = renderer.ShutdownAndValidate();
        Check(report.liveChildren == 0 && report.priorWarnings == 0,"Static model cache leaked D3D resources");
        std::cout << "Shared scenes, transactional reload, recovery, explicit eviction and destruction passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
