#include "GravityGame.hpp"
#include <remi/runtime/Application.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/resources/ResourceManager.hpp>
#include <remi/core/Log.hpp>
#include <array>
#include <cmath>
#include <sstream>

std::unique_ptr<remi::Mesh> Box(remi::Renderer& renderer,remi::Vec3 color) {
    std::array<remi::Vertex,8> vertices{};
    for (unsigned i = 0; i < 8; ++i) vertices[i] = {{(i&1) ? .5f:-.5f,(i&2) ? .5f:-.5f,(i&4) ? .5f:-.5f},color};
    const std::array<std::uint32_t,36> indices{0,2,1,1,2,3,5,7,4,4,7,6,4,6,0,0,6,2,1,3,5,5,3,7,2,6,3,3,6,7,4,0,5,5,0,1};
    return renderer.CreateMesh(vertices,indices);
}
class GravityApp final : public remi::Application {
public:
    bool smoke = false, warp = false, failed = false;
    std::filesystem::path capture;
    void OnStart(remi::Window& window) override {
        remi::ResourceManager files(remi::ExecutableDirectory());
        const auto shader = files.LoadFile("shaders/Basic.hlsl"); const auto& bytes = files.Get(shader)->bytes;
        if (bytes.empty()) throw std::runtime_error("Empty shader");
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
        config.shaderSource.assign(bytes.begin(),bytes.end()); config.useWarp = warp; config.vsync = !smoke;
        auto renderer = std::make_unique<remi::Renderer>(config);
        auto meshes = std::make_unique<remi::ResourceCache<remi::Mesh>>();
        auto platform = meshes->Load("platform",[&] { return Box(*renderer,{.22f,.30f,.42f}); });
        auto player = meshes->Load("player",[&] { return Box(*renderer,{1,.42f,.07f}); });
        auto goal = meshes->Load("goal",[&] { return Box(*renderer,{.05f,1,.25f}); });
        auto game = std::make_unique<remi::game::GravityGame>(platform,player,goal);
        if (smoke) {
            game->Tick({0,0,true},1.f/60);
            for (unsigned i = 0; i < 70; ++i) game->Tick({},1.f/60);
            for (unsigned i = 0; i < 180; ++i) game->Tick({1,0},1.f/60);
            game->Tick({0,0,true},1.f/60);
            for (unsigned i = 0; i < 70; ++i) game->Tick({},1.f/60);
            if (game->Status() != remi::game::State::Won) throw std::runtime_error("Game route smoke failed");
            remi::Log("Game smoke: ceiling route, 2 gravity flips, goal reached");
            game->Restart();
        }
        renderer_ = std::move(renderer); meshes_ = std::move(meshes); game_ = std::move(game);
        camera_.yaw = -.25f; camera_.pitch = .22f; camera_.distance = 18;
        camera_.target = {-2,1.8f,0};
    }
    void OnResize(unsigned w,unsigned h) override { renderer_->Resize(w,h); }
    void OnFixedUpdate(remi::Window& window,const remi::Input& input,double dt) override {
        if (input.Pressed(remi::Key::Escape)) window.RequestClose();
        game_->Tick({float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A)),float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S)),input.Pressed(remi::Key::Space),input.Pressed(remi::Key::Enter)},static_cast<float>(dt));
        if (input.Held(remi::Key::MouseRight)) camera_.Orbit(static_cast<float>(input.DeltaX()),static_cast<float>(input.DeltaY()));
        camera_.Zoom(input.Wheel());
        if (input.Pressed(remi::Key::F1)) { camera_.yaw = -.25f; camera_.pitch = .22f; camera_.distance = 18; }
    }
    void OnFrame(remi::Window& window,double elapsed,double) override {
        const auto p = game_->Position();
        const float blend = 1-std::exp(-5*static_cast<float>(elapsed));
        camera_.target.x += (p.x*.35f-camera_.target.x)*blend;
        const auto vp = camera_.ViewProjection(float(renderer_->Width())/float(renderer_->Height()));
        const remi::DirectionalLight light;
        const auto lightVP = remi::DirectionalShadowMatrix({0,2,0},light.direction,24);
        const auto resolve = [&](remi::MeshHandle handle) { return meshes_->Get(handle); };
        renderer_->BeginShadow(lightVP); (void)remi::DrawScene(*renderer_,game_->World(),lightVP,resolve); renderer_->EndShadow();
        const auto stats = remi::DrawScene(*renderer_,game_->World(),vp,resolve,&light);
        if (stats.missing || renderer_->CheckDiagnostics()) throw std::runtime_error("Game render validation failed");
        if (!capture.empty() && frame_ == 4) renderer_->SaveScreenshot(capture);
        renderer_->Present(); ++frame_;
        const auto state = game_->Status();
        std::wostringstream title;
        title << L"REMI Gravity | " << (state == remi::game::State::Won ? L"CLEAR! Enter to replay" : state == remi::game::State::Lost ? L"FELL! Enter to retry" : L"Reach the GREEN pad using the ceiling")
              << L" | Gravity " << (game_->Inverted() ? L"UP" : L"DOWN") << L" | " << (game_->Supported() ? L"Space: flip ready" : L"Airborne")
              << L" | WASD move / Space flip / Enter restart / RMB camera / Wheel zoom / Esc";
        window.SetTitle(title.str());
    }
    void OnStop() noexcept override {
        game_.reset(); meshes_.reset();
        try { if (renderer_) { const auto audit = renderer_->ShutdownAndValidate(); failed = audit.liveChildren != 0 || audit.priorWarnings != 0;
            remi::Log("Game GPU shutdown: live children="+std::to_string(audit.liveChildren)+", warnings="+std::to_string(audit.priorWarnings)); } }
        catch (const std::exception& e) { failed = true; remi::Log(e.what()); }
        renderer_.reset();
    }
private:
    std::unique_ptr<remi::Renderer> renderer_;
    std::unique_ptr<remi::ResourceCache<remi::Mesh>> meshes_;
    std::unique_ptr<remi::game::GravityGame> game_;
    remi::Camera camera_;
    unsigned frame_ = 0;
};
int wmain(int argc,wchar_t** argv) {
    remi::RunConfig config; GravityApp app;
    config.window.title = L"REMI Gravity | WASD move | Space flip | Enter restart";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") app.smoke = true;
        else if (arg == L"--warp") app.warp = true;
        else if (arg == L"--capture" && i+1 < argc) app.capture = argv[++i];
        else return 2;
    }
    if (app.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 12; }
    const int result = remi::RunApplication(app,config); return result ? result : (app.failed ? 1 : 0);
}
