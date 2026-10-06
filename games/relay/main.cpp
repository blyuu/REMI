#include "RelayGame.hpp"
#include <remi/core/Log.hpp>
#include <remi/debug/SettingsMenu.hpp>
#include <remi/render/PrimitiveMesh.hpp>
#include <remi/render/SceneRenderSession.hpp>
#include <remi/resources/ResourceCache.hpp>
#include <remi/runtime/Application.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

class RelayLayer final : public remi::Layer {
public:
    bool smoke = false, warp = false, failed = false;
    std::filesystem::path capture;

    void OnAttach(remi::Window& window) override {
        remi::RendererConfig config;
        config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
        config.shaderFile = remi::ExecutableDirectory() / "shaders/Basic.hlsl";
        config.useWarp = warp; config.vsync = !smoke;
        auto session = std::make_unique<remi::SceneRenderSession>(config);
        auto& renderer = session->Device();
        auto& meshes = session->Meshes();
        const auto mesh = [&](const char* name, remi::Vec3 color) {
            return meshes.Load(name,[&] { return remi::CreateBoxMesh(renderer,color); });
        };
        const remi::relay::RelayMeshes handles{
            mesh("floor",{.18f,.25f,.36f}), mesh("obstacle",{.75f,.28f,.20f}),
            mesh("player",{.92f,.88f,.76f}), mesh("switch",{.05f,.80f,.92f}),
            mesh("goal",{.22f,.92f,.38f})};
        auto game = std::make_unique<remi::relay::RelayGame>(handles);
        if (smoke) {
            for (unsigned i = 0; i < 18; ++i) game->Tick({0,1},1.f/60);
            for (unsigned i = 0; i < 90; ++i) game->Tick({1,0},1.f/60);
            if (!game->SwitchActive()) throw std::runtime_error("Relay smoke did not activate switch");
            for (unsigned i = 0; i < 18; ++i) game->Tick({0,-1},1.f/60);
            for (unsigned i = 0; i < 90; ++i) game->Tick({1,0},1.f/60);
            if (game->Result() != remi::relay::Status::Won) throw std::runtime_error("Relay smoke did not reach goal");
        }
        render_ = std::move(session); renderer_ = &render_->Device(); game_ = std::move(game);
        settings_ = std::make_unique<remi::ui::SettingsMenu>(
            remi::ExecutableDirectory()/"assets/fonts/NotoSansKR-VF.ttf",L"REMI RELAY",
            std::vector<std::wstring>{L"WASD   이동",L"마우스 우클릭   카메라 회전",
                L"마우스 휠   확대 / 축소",L"Enter   다시 시작",L"Esc   종료"});
        camera_.target = {0,0,0}; camera_.yaw = -.3f; camera_.pitch = .48f; camera_.distance = 21;
    }
    void OnResize(unsigned width, unsigned height) override { renderer_->Resize(width,height); }
    void OnFixedUpdate(remi::Window& window, const remi::Input& input, double dt) override {
        if (input.Pressed(remi::Key::Escape)) {
            if (settings_->Open()) settings_->SetOpen(false);
            else window.RequestClose();
        }
        if (settings_->HandleClick(input,window.Width(),window.Height()) || settings_->Open()) return;
        if (!smoke) {
            const float x = float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A));
            const float z = float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S));
            game_->Tick({x,z,input.Pressed(remi::Key::Enter)},static_cast<float>(dt));
            if (input.Held(remi::Key::MouseRight))
                camera_.Orbit(static_cast<float>(input.DeltaX()),static_cast<float>(input.DeltaY()));
            camera_.Zoom(input.Wheel());
        }
    }
    void OnFrame(remi::Window&, double elapsed, double) override {
        const auto vp = camera_.ViewProjection(float(renderer_->Width())/float(renderer_->Height()));
        remi::DirectionalLight light; light.ambient = .34f; light.cameraPosition = camera_.Position();
        const auto lightVP = remi::DirectionalShadowMatrix({0,0,0},light.direction,22);
        (void)render_->DrawShadow(game_->World(),lightVP);
        (void)render_->DrawColor(game_->World(),vp,light);
        overlayAge_ += elapsed;
        if (overlayAge_ >= .2 || !overlayReady_) {
            overlayAge_ = 0; overlayReady_ = true;
            std::wostringstream timer; timer << std::fixed << std::setprecision(1)
                << L"남은 시간  " << std::max(0.f,game_->Remaining()) << L"초";
            const std::wstring status = game_->Result() == remi::relay::Status::Won ? L"완료! Enter로 다시 시작" :
                game_->Result() == remi::relay::Status::TimedOut ? L"시간 종료 · Enter로 다시 시작" :
                game_->Result() == remi::relay::Status::Fell ? L"추락 · Enter로 다시 시작" :
                game_->SwitchActive() ? L"스위치 작동 · 초록 출구로 이동" : L"파란 스위치를 먼저 누르세요";
            settings_->SetStatus({timer.str(),status});
        }
        settings_->Update(*renderer_,renderer_->Width(),renderer_->Height());
        settings_->Draw(*renderer_);
        render_->RequireCleanDiagnostics();
        if (!capture.empty() && frame_ == 4) renderer_->SaveScreenshot(capture);
        renderer_->Present(); ++frame_;
    }
    void OnDetach() noexcept override {
        settings_->Clear(); settings_.reset(); game_.reset();
        try {
            if (render_) {
                const auto audit = render_->ShutdownAndValidate();
                failed = audit.liveChildren != 0 || audit.priorWarnings != 0;
            }
        } catch (const std::exception& error) { failed = true; remi::Log(error.what()); }
        render_.reset(); renderer_ = nullptr;
    }
private:
    std::unique_ptr<remi::SceneRenderSession> render_;
    remi::Renderer* renderer_ = nullptr; // Non-owning alias, valid while render_ is alive.
    std::unique_ptr<remi::relay::RelayGame> game_;
    remi::Camera camera_;
    std::unique_ptr<remi::ui::SettingsMenu> settings_;
    double overlayAge_ = 1;
    bool overlayReady_ = false;
    unsigned frame_ = 0;
};

int wmain(int argc, wchar_t** argv) {
    remi::RunConfig config;
    auto layer = std::make_unique<RelayLayer>(); auto& relay = *layer;
    config.window.title = L"REMI Relay";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") relay.smoke = true;
        else if (arg == L"--warp") relay.warp = true;
        else if (arg == L"--capture" && i+1 < argc) relay.capture = argv[++i];
        else return 2;
    }
    if (relay.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 12; }
    remi::Application application; application.PushLayer(std::move(layer));
    const int result = remi::RunApplication(application,config);
    return result ? result : (relay.failed ? 1 : 0);
}
