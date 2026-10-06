#include <remi/runtime/Application.hpp>
#include <remi/core/BuildInfo.hpp>
#include <remi/core/Log.hpp>
#include <remi/render/Renderer.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/resources/ResourceManager.hpp>
#include <remi/physics/PhysicsWorld.hpp>
#include <remi/assets/StaticGltfCache.hpp>
#include <remi/debug/SceneInspector.hpp>
#include <cmath>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
std::unique_ptr<remi::Mesh> Cube(remi::Renderer& renderer) {
    using remi::Vec3;
    const std::array<std::array<Vec3, 4>, 6> faces{{
        {{{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f}}},
        {{{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f}}},
        {{{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f}}},
        {{{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f}}},
        {{{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f}}},
        {{{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,.5f},{.5f,-.5f,-.5f}}}
    }};
    const std::array<Vec3, 6> colors{{{.10f,.38f,.70f},{.24f,.16f,.55f},{.08f,.23f,.40f},
                                    {.08f,.52f,.42f},{.32f,.68f,.90f},{.06f,.10f,.16f}}};
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (unsigned face = 0; face < 6; ++face) {
        for (const auto point : faces[face]) vertices.push_back({point, colors[face]});
        for (const unsigned index : {0u,1u,2u,2u,1u,3u}) indices.push_back(face * 4 + index);
    }
    return renderer.CreateMesh(vertices, indices);
}
std::unique_ptr<remi::Mesh> Floor(remi::Renderer& renderer) {
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int z = -5; z < 5; ++z) for (int x = -5; x < 5; ++x) {
        const float a = static_cast<float>(x), b = static_cast<float>(z);
        const remi::Vec3 color = ((x + z) % 2 == 0) ? remi::Vec3{.09f,.12f,.16f} : remi::Vec3{.16f,.20f,.25f};
        const auto base = static_cast<std::uint32_t>(vertices.size());
        for (const auto point : {remi::Vec3{a,0,b},remi::Vec3{a,0,b+1},remi::Vec3{a+1,0,b},remi::Vec3{a+1,0,b+1}})
            vertices.push_back({point, color});
        for (const unsigned index : {0u,1u,2u,2u,1u,3u}) indices.push_back(base + index);
    }
    return renderer.CreateMesh(vertices, indices);
}
}

class Sandbox final : public remi::Application {
public:
    bool smoke = false, warp = false, inspect = false;
    std::filesystem::path capture, staticGltf;
    void OnStart(remi::Window& window) override {
        const auto info = remi::GetBuildInfo();
        remi::Log(std::string("Sandbox ") + std::string(info.version) + " / " + std::string(info.configuration));
        remi::RendererConfig config;
        config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
        config.shaderFile = remi::ExecutableDirectory() / "shaders/Basic.hlsl";
        auto resources = std::make_unique<remi::ResourceManager>(remi::ExecutableDirectory());
        const auto shader = resources->LoadFile("shaders/Basic.hlsl");
        const auto& bytes = resources->Get(shader)->bytes;
        if (bytes.empty()) throw std::runtime_error("Empty shader resource");
        config.shaderSource.assign(bytes.begin(),bytes.end());
        config.useWarp = warp; config.vsync = !smoke;
        auto renderer = std::make_unique<remi::Renderer>(config);
        auto meshes = std::make_unique<remi::ResourceCache<remi::Mesh>>();
        const auto cube = meshes->Load("builtin/cube",[&] { return Cube(*renderer); });
        const auto floor = meshes->Load("builtin/floor",[&] { return Floor(*renderer); });
        if (meshes->Load("builtin/cube",[&] { return Cube(*renderer); }) != cube || renderer->LiveMeshes() != 2)
            throw std::logic_error("Mesh cache did not deduplicate");
        auto scene = std::make_unique<remi::Scene>();
        const auto add = [&](const char* name, remi::MeshHandle mesh, remi::Vec3 position, remi::Vec3 scale, float yaw = 0) {
            const auto id = scene->Create(name);
            *scene->Get<remi::TransformComponent>(id) = {position,{0,yaw,0},scale};
            scene->Add<remi::MeshComponent>(id, {mesh,true}); return id;
        };
        (void)add("Checker floor",floor,{0,0,0},{1,1,1});
        const auto group = scene->Create("Rotating group");
        const auto main = add("Main cube",cube,{0,.75f,0},{1.5f,1.5f,1.5f});
        if (!scene->SetParent(main, group)) throw std::logic_error("Failed to parent main cube");
        (void)add("Left cube",cube,{-2,.45f,1},{.9f,.9f,.9f},-.4f);
        (void)add("Right column",cube,{2,.8f,1.8f},{.8f,1.6f,.8f},.3f);
        const auto child = add("Orbiting child",cube,{2,1.2f,0},{.35f,.35f,.35f});
        if (!scene->SetParent(child, group)) throw std::logic_error("Failed to parent child");
        std::vector<remi::EntityId> extras{child};
        auto physics = std::make_unique<remi::PhysicsWorld>(*scene);
        const auto ground = scene->Create("Physics ground");
        scene->Get<remi::TransformComponent>(ground)->position = {0,-.25f,0};
        physics->AddBody(ground,{remi::BodyType::Static,{5,.25f,5}});
        const auto falling = add("Falling physics cube",cube,{2.5f,3,-1.5f},{.7f,.7f,.7f});
        physics->AddBody(falling,{remi::BodyType::Dynamic,{.35f,.35f,.35f}});
        if (smoke) {
            for (unsigned tick = 0; tick < 120; ++tick) physics->Step(1.f/60);
            const float y = scene->Get<remi::TransformComponent>(falling)->position.y;
            if (std::abs(y-.35f) > .001f || !physics->Supported(falling)) throw std::runtime_error("Physics sample settling failed");
            remi::Log("Physics smoke: 120 fixed ticks, cube y=" + std::to_string(y) + ", supported=true");
        }
        renderer_ = std::move(renderer); meshes_ = std::move(meshes); cube_ = cube; resources_ = std::move(resources);
        scene_ = std::move(scene); group_ = group; extras_ = std::move(extras);
        physics_ = std::move(physics); falling_ = falling;
        assets_ = std::make_unique<remi::assets::StaticGltfCache>(*renderer_,*meshes_);
        if (!staticGltf.empty()) (void)assets_->Instantiate(*scene_,staticGltf);
        inspector_.SetVisible(inspect);
        inspector_.Update(*scene_,{},0);
    }
    void OnResize(unsigned width, unsigned height) override { renderer_->Resize(width, height); }
    void OnFixedUpdate(remi::Window& window, const remi::Input& input, double step) override {
        if (input.Pressed(remi::Key::Escape)) window.RequestClose();
        inspector_.Update(*scene_,input,static_cast<float>(step));
        if (input.Pressed(remi::Key::F5) && !staticGltf.empty()) {
            try { assets_->Reload(staticGltf); remi::Log("Static glTF reloaded; instance handles preserved"); }
            catch (const std::exception& error) { remi::Log(std::string("Static reload failed; previous asset retained: ")+error.what()); }
        }
        if (inspector_.Visible()) {
            if (input.Held(remi::Key::MouseRight)) camera_.Orbit(static_cast<float>(input.DeltaX()),static_cast<float>(input.DeltaY()));
            camera_.Zoom(input.Wheel());
            previousAngle_ = angle_;
            return;
        }
        if (input.Pressed(remi::Key::Space)) spinning_ = !spinning_;
        if (input.Pressed(remi::Key::F1)) camera_ = remi::Camera{};
        if (input.Pressed(remi::Key::Q) && extras_.size() < 32) {
            const auto id = scene_->Create("Spawned child");
            const float theta = static_cast<float>(extras_.size()) * .7f;
            *scene_->Get<remi::TransformComponent>(id) = {{std::cos(theta)*2,1.2f,std::sin(theta)*2},{},{.35f,.35f,.35f}};
            scene_->Add<remi::MeshComponent>(id,{cube_,true});
            if (!scene_->SetParent(id,group_)) throw std::logic_error("Failed to parent spawned child");
            extras_.push_back(id);
        }
        if (input.Pressed(remi::Key::E) && !extras_.empty()) {
            scene_->Destroy(extras_.back()); extras_.pop_back();
        }
        if (input.Held(remi::Key::MouseRight)) camera_.Orbit(static_cast<float>(input.DeltaX()), static_cast<float>(input.DeltaY()));
        camera_.Zoom(input.Wheel());
        const float movement = static_cast<float>(step) * 3;
        camera_.target.x += (static_cast<float>(input.Held(remi::Key::D)) - static_cast<float>(input.Held(remi::Key::A))) * movement;
        camera_.target.z += (static_cast<float>(input.Held(remi::Key::W)) - static_cast<float>(input.Held(remi::Key::S))) * movement;
        previousAngle_ = angle_;
        if (spinning_) angle_ += static_cast<float>(step) * .5f;
        if (input.Pressed(remi::Key::Enter)) {
            scene_->Get<remi::TransformComponent>(falling_)->position = {2.5f,3,-1.5f};
            physics_->SetVelocity(falling_,{});
        }
        physics_->Step(static_cast<float>(step));
    }
    void OnFrame(remi::Window& window, double elapsed, double alpha) override {
        const auto vp = camera_.ViewProjection(static_cast<float>(renderer_->Width()) / static_cast<float>(renderer_->Height()));
        const float angle = smoke ? .4f : previousAngle_ + (angle_ - previousAngle_) * static_cast<float>(alpha);
        scene_->Get<remi::TransformComponent>(group_)->rotation.y = angle;
        const remi::DirectionalLight light;
        const auto lightMatrix = remi::DirectionalShadowMatrix({0,0,0},light.direction);
        const auto resolver = [&](remi::MeshHandle key) { return meshes_->Get(key); };
        renderer_->BeginShadow(lightMatrix);
        const auto shadowStats = remi::DrawScene(*renderer_,*scene_,lightMatrix,resolver);
        renderer_->EndShadow();
        const auto stats = remi::DrawScene(*renderer_, *scene_, vp, [&](remi::MeshHandle key) -> const remi::Mesh* {
            return meshes_->Get(key);
        }, &light);
        if (stats.missing) throw std::logic_error("Scene contains an unresolved mesh key");
        inspector_.Draw(*renderer_,*scene_);
        if (smoke && frames_ == 4) remi::Log("Render passes: shadow draws=" + std::to_string(shadowStats.draws) +
            ", color draws=" + std::to_string(stats.draws) + ", color culled=" + std::to_string(stats.culled));
        if (!capture.empty() && frames_ == 4) renderer_->SaveScreenshot(capture);
        if (renderer_->CheckDiagnostics()) throw std::runtime_error("D3D11 validation reported warnings/errors");
        renderer_->Present(); ++frames_; titleTimer_ += elapsed;
        if (renderer_->CheckDiagnostics()) throw std::runtime_error("D3D11 Present validation failed");
        if (titleTimer_ >= .25) {
            titleTimer_ = 0;
            std::wostringstream title;
            title << L"REMI Phase 7 | Contacts " << physics_->Stats().contacts << L" / Supported " << physics_->Supported(falling_) << L" / Draws " << stats.draws << L" / Shadow " << shadowStats.draws
                  << L" | Enter drop cube | Q add / E delete | F3 Inspector | F5 reload asset | RMB orbit / Wheel zoom / WASD pan / Space pause / F1 reset / Esc";
            window.SetTitle(title.str());
        }
    }
    void OnStop() noexcept override {
        inspector_.Clear(); extras_.clear(); physics_.reset(); scene_.reset(); assets_.reset(); meshes_.reset(); resources_.reset();
        try {
            if (renderer_) {
                const auto audit = renderer_->ShutdownAndValidate();
                remi::Log(std::string("GPU shutdown audit: ") + (audit.debugValidated ? "validated" : "debug unavailable") +
                    ", live children=" + std::to_string(audit.liveChildren) + ", prior warnings=" + std::to_string(audit.priorWarnings));
                shutdownFailed = audit.liveChildren != 0 || audit.priorWarnings != 0;
            }
        } catch (const std::exception& error) { shutdownFailed = true; remi::Log(error.what()); }
        renderer_.reset();
    }
    bool shutdownFailed = false;
private:
    std::unique_ptr<remi::Renderer> renderer_;
    std::unique_ptr<remi::ResourceManager> resources_;
    std::unique_ptr<remi::ResourceCache<remi::Mesh>> meshes_;
    std::unique_ptr<remi::assets::StaticGltfCache> assets_;
    remi::debug::SceneInspector inspector_;
    remi::MeshHandle cube_;
    std::unique_ptr<remi::Scene> scene_;
    std::unique_ptr<remi::PhysicsWorld> physics_;
    remi::EntityId falling_;
    remi::EntityId group_;
    std::vector<remi::EntityId> extras_;
    remi::Camera camera_;
    double titleTimer_ = 0;
    float angle_ = 0, previousAngle_ = 0;
    bool spinning_ = true;
    unsigned frames_ = 0;
};

int wmain(int argc, wchar_t** argv) {
    remi::RunConfig config; Sandbox application;
    config.window.title = L"REMI Phase 7 | Enter drop cube | Q add / E delete | RMB orbit / Wheel zoom / WASD pan / Space pause / F1 reset / Esc";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") application.smoke = true;
        else if (arg == L"--inspector") application.inspect = true;
        else if (arg == L"--static-gltf" && i+1 < argc) application.staticGltf = argv[++i];
        else if (arg == L"--warp") application.warp = true;
        else if (arg == L"--capture" && i + 1 < argc) application.capture = argv[++i];
        else { remi::Log("Usage: REMISandbox [--smoke] [--warp] [--inspector] [--static-gltf path] [--capture path.bmp]"); return 2; }
    }
    if (application.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 12; }
    const int result = remi::RunApplication(application, config);
    return result ? result : (application.shutdownFailed ? 1 : 0);
}
