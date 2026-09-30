#include "GravityGame.hpp"
#include "GameHud.hpp"
#include <remi/debug/DebugOverlay.hpp>
#include <remi/core/Profiler.hpp>
#include <remi/runtime/Application.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/render/BakedAnimation.hpp>
#include <remi/resources/ResourceManager.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/BuildInfo.hpp>
#include <array>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iomanip>

std::unique_ptr<remi::Mesh> Box(remi::Renderer& renderer,remi::Vec3 color) {
    std::array<remi::Vertex,8> vertices{};
    for (unsigned i = 0; i < 8; ++i) vertices[i] = {{(i&1) ? .5f:-.5f,(i&2) ? .5f:-.5f,(i&4) ? .5f:-.5f},color};
    const std::array<std::uint32_t,36> indices{0,2,1,1,2,3,5,7,4,4,7,6,4,6,0,0,6,2,1,3,5,5,3,7,2,6,3,3,6,7,4,0,5,5,0,1};
    return renderer.CreateMesh(vertices,indices);
}
std::unique_ptr<remi::Mesh> Coin(remi::Renderer& renderer) {
    constexpr unsigned sides = 24;
    constexpr float pi = 3.14159265f;
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(sides*4+2); indices.reserve(sides*12);
    const remi::Vec3 gold{.95f,.59f,.09f};
    vertices.push_back({{0,0,-.07f},gold,{0,0,-1}});
    vertices.push_back({{0,0,.07f},gold,{0,0,1}});
    for (unsigned i = 0; i < sides; ++i) {
        const float angle = 2*pi*static_cast<float>(i)/sides;
        const float x = std::cos(angle)*.33f, y = std::sin(angle)*.33f;
        vertices.push_back({{x,y,-.07f},gold,{0,0,-1}});
        vertices.push_back({{x,y,.07f},gold,{0,0,1}});
        vertices.push_back({{x,y,-.07f},gold,{std::cos(angle),std::sin(angle),0}});
        vertices.push_back({{x,y,.07f},gold,{std::cos(angle),std::sin(angle),0}});
    }
    for (unsigned i = 0; i < sides; ++i) {
        const unsigned a = 2+i*4, b = 2+((i+1)%sides)*4;
        indices.insert(indices.end(),{0,b,a,1,a+1,b+1,a+2,b+2,a+3,a+3,b+2,b+3});
    }
    return renderer.CreateMesh(vertices,indices);
}
class GravityApp final : public remi::Application {
public:
    bool smoke = false, warp = false, failed = false;
    bool previewFinish = false, previewFell = false;
    unsigned profileFrames = 12;
    std::filesystem::path capture;
    std::filesystem::path profileCsv;
    std::filesystem::path demoDirectory;
    std::filesystem::path characterFile;
    std::string idleClip = "idle", moveClip = "run";
    void OnStart(remi::Window& window) override {
        remi::ResourceManager files(remi::ExecutableDirectory());
        const auto shader = files.LoadFile("shaders/Basic.hlsl"); const auto& bytes = files.Get(shader)->bytes;
        if (bytes.empty()) throw std::runtime_error("Empty shader");
        remi::RendererConfig config; config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
        config.shaderSource.assign(bytes.begin(),bytes.end()); config.useWarp = warp; config.vsync = !smoke && demoDirectory.empty();
        if (!demoDirectory.empty()) std::filesystem::create_directories(demoDirectory);
        auto renderer = std::make_unique<remi::Renderer>(config);
        auto meshes = std::make_unique<remi::ResourceCache<remi::Mesh>>();
        auto platform = meshes->Load("platform",[&] { return Box(*renderer,{.22f,.30f,.42f}); });
        const auto selectedCharacter = characterFile.empty() ? remi::ExecutableDirectory() / "assets/characters/default.rmc" : characterFile;
        if (!characterFile.empty() && !std::filesystem::exists(selectedCharacter)) throw std::runtime_error("Character asset not found: " + selectedCharacter.string());
        auto animation = std::filesystem::exists(selectedCharacter) ? remi::BakedAnimation::Load(selectedCharacter) : nullptr;
        if (animation && (!animation->HasClip(idleClip) || !animation->HasClip(moveClip)))
            throw std::runtime_error("Character asset must contain selected idle and move clips");
        auto player = meshes->Load("player",[&] { return animation ? animation->CreateMesh(*renderer) : Box(*renderer,{1,.42f,.07f}); });
        auto goal = meshes->Load("goal",[&] { return Box(*renderer,{.05f,1,.25f}); });
        auto coin = meshes->Load("coin",[&] { return Coin(*renderer); });
        auto accent = meshes->Load("accent",[&] { return Box(*renderer,{.03f,.67f,.76f}); });
        auto game = std::make_unique<remi::game::GravityGame>(platform,player,goal,animation != nullptr,coin,accent);
        remi::Log(animation ? "Character asset loaded: " + selectedCharacter.string() + " (" + idleClip + ", " + moveClip + ")" : "No character asset: using box player");
        if (smoke) {
            if (previewFell) {
                for (unsigned i = 0; i < 200; ++i) game->Tick({1,0},1.f/60);
                if (game->Status() != remi::game::State::Lost) throw std::runtime_error("Game fall preview failed");
            } else {
                game->Tick({0,0,true},1.f/60);
                for (unsigned i = 0; i < 70; ++i) game->Tick({},1.f/60);
                for (unsigned i = 0; i < 180; ++i) game->Tick({1,0},1.f/60);
                game->Tick({0,0,true},1.f/60);
                for (unsigned i = 0; i < 70; ++i) game->Tick({},1.f/60);
                if (game->Status() != remi::game::State::Won) throw std::runtime_error("Game route smoke failed");
                remi::Log("Game smoke: 3 coins, 2 gravity flips, goal reached");
                if (!previewFinish) game->Restart();
            }
        }
        renderer_ = std::move(renderer); meshes_ = std::move(meshes); game_ = std::move(game);
        animation_ = std::move(animation); playerHandle_ = player;
        hud_ = std::make_unique<GameHud>(remi::ExecutableDirectory() / "assets/fonts/Pretendard-SemiBold.otf");
        if (!profileCsv.empty()) {
            csv_.open(profileCsv,std::ios::binary);
            if (!csv_) throw std::runtime_error("Cannot open profile CSV");
            csv_ << "# REMI 0.11.0," << (warp ? "WARP" : "HARDWARE") << ',' << config.width << 'x' << config.height
                 << ",vsync=" << config.vsync << ",build=" << remi::GetBuildInfo().configuration << '\n';
            csv_ << "frame,frame_ms,fps,cpu_physics_ms,cpu_shadow_ms,cpu_color_ms,cpu_present_ms,gpu_valid,gpu_sample_id,gpu_shadow_ms,gpu_color_ms,gpu_total_ms,shadow_draws,color_draws,culled,entities,bodies,contacts,scene_slot_bytes,meshes\n";
        }
        camera_.yaw = -.25f; camera_.pitch = .22f; camera_.distance = 14;
        camera_.target = {-2,1.8f,0};
    }
    void OnResize(unsigned w,unsigned h) override { renderer_->Resize(w,h); }
    void OnFixedUpdate(remi::Window& window,const remi::Input& input,double dt) override {
        if (input.Pressed(remi::Key::Escape)) window.RequestClose();
        const auto physicsStart = remi::CpuProfiler::Clock::now();
        float moveX = float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A));
        float moveZ = float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S));
        bool flip = input.Pressed(remi::Key::Space);
        if (!demoDirectory.empty()) {
            moveX = demoTick_ >= 101 && demoTick_ < 281 ? 1.f : 0.f;
            moveZ = 0;
            flip = demoTick_ == 30 || demoTick_ == 281;
        }
        game_->Tick({moveX,moveZ,flip,input.Pressed(remi::Key::Enter) && demoDirectory.empty()},static_cast<float>(dt));
        if (demoDirectory.empty() && input.Pressed(remi::Key::Q)) game_->CyclePlayerMaterial();
        if (!demoDirectory.empty()) {
            if (demoTick_ == 215) { game_->CyclePlayerMaterial(); game_->CyclePlayerMaterial(); }
            ++demoTick_;
            if (demoTick_ >= 410) {
                if (game_->Status() != remi::game::State::Won) throw std::runtime_error("Demo route did not finish");
                window.RequestClose();
            }
        }
        running_ = game_->Status() == remi::game::State::Playing && (moveX != 0 || moveZ != 0);
        physicsMs_ += std::chrono::duration<double,std::milli>(remi::CpuProfiler::Clock::now()-physicsStart).count();
        if (input.Pressed(remi::Key::F2)) { showDebug_ = !showDebug_; if (!showDebug_) overlay_.Clear(); overlayAge_ = 1; }
        if (input.Held(remi::Key::MouseRight)) camera_.Orbit(static_cast<float>(input.DeltaX()),static_cast<float>(input.DeltaY()));
        camera_.Zoom(input.Wheel());
        if (input.Pressed(remi::Key::F1)) { camera_.yaw = -.25f; camera_.pitch = .22f; camera_.distance = 14; }
    }
    void OnFrame(remi::Window& window,double elapsed,double) override {
        profiler_.BeginFrame(); profiler_.Record(remi::CpuStage::Physics,physicsMs_); physicsMs_ = 0;
        const auto p = game_->Position();
        const float blend = 1-std::exp(-5*static_cast<float>(elapsed));
        camera_.target.x += (p.x*.35f-camera_.target.x)*blend;
        const auto vp = camera_.ViewProjection(float(renderer_->Width())/float(renderer_->Height()));
        remi::DirectionalLight light; light.ambient = .38f; light.cameraPosition = camera_.Position();
        const auto lightVP = remi::DirectionalShadowMatrix({0,2,0},light.direction,24);
        const auto resolve = [&](remi::MeshHandle handle) { return meshes_->Get(handle); };
        if (animation_) animation_->Update(*renderer_,*meshes_->GetMutable(playerHandle_),running_ ? moveClip : idleClip,elapsed);
        renderer_->BeginGpuProfile();
        const auto shadowStart = remi::CpuProfiler::Clock::now();
        renderer_->BeginShadow(lightVP); const auto shadowStats = remi::DrawScene(*renderer_,game_->World(),lightVP,resolve); renderer_->EndShadow();
        profiler_.Add(remi::CpuStage::Shadow,shadowStart);
        const auto colorStart = remi::CpuProfiler::Clock::now();
        const auto stats = remi::DrawScene(*renderer_,game_->World(),vp,resolve,&light);
        profiler_.Add(remi::CpuStage::Color,colorStart);
        if (stats.missing) throw std::runtime_error("Game render validation failed");
        hud_->Update(*renderer_,renderer_->Width(),renderer_->Height(),*game_);
        hud_->Draw(*renderer_);
        overlayAge_ += elapsed;
        if (showDebug_) {
            if (overlayAge_ >= .2 || (smoke && frame_ == 4)) {
                overlayAge_ = 0;
                const auto cpu = profiler_.Snapshot(); const auto gpu = renderer_->PollGpuProfile();
                const auto phys = game_->Physics(); const auto mem = game_->World().Memory();
                std::vector<std::string> lines;
                const auto line = [&](const auto& builder) { std::ostringstream out; out << std::fixed << std::setprecision(2); builder(out); lines.push_back(out.str()); };
                lines.push_back("REMI DEBUG  F2 HIDE  0.11.0");
                line([&](auto& o) { o << "FPS " << cpu.fps << " FRAME " << cpu.frameMs << " MS"; });
                line([&](auto& o) { o << "CPU PHYS " << cpu.milliseconds[0] << " SHAD " << cpu.milliseconds[1] << " MS"; });
                line([&](auto& o) { o << "CPU COLOR " << cpu.milliseconds[2] << " PRESENT " << cpu.milliseconds[3] << " MS"; });
                line([&](auto& o) { if (gpu.valid) o << "GPU SHAD " << gpu.shadowMs << " COLOR " << gpu.colorMs << " MS"; else o << "GPU WAITING FOR SAMPLE"; });
                line([&](auto& o) { o << "DRAW SHAD " << shadowStats.draws << " COLOR " << stats.draws << " CULL " << stats.culled; });
                line([&](auto& o) { o << "ENTITY " << mem.entities << " BODY " << phys.bodies << " CONTACT " << phys.contacts; });
                line([&](auto& o) { o << "SCENE SLOT " << mem.slotCapacityBytes << " B MESH " << meshes_->Size(); });
                overlay_.Update(*renderer_,renderer_->Width(),renderer_->Height(),lines);
            }
            overlay_.Draw(*renderer_);
        }
        if (renderer_->CheckDiagnostics()) throw std::runtime_error("Game render validation failed");
        renderer_->EndGpuProfile();
        if (!capture.empty() && frame_ == 4) renderer_->SaveScreenshot(capture);
        if (!demoDirectory.empty() && demoTick_ >= nextDemoCaptureTick_) {
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << demoFrames_++ << ".bmp";
            renderer_->SaveScreenshot(demoDirectory / name.str());
            nextDemoCaptureTick_ += 6;
        }
        const auto presentStart = remi::CpuProfiler::Clock::now();
        renderer_->Present(); profiler_.Add(remi::CpuStage::Present,presentStart);
        profiler_.EndFrame(elapsed);
        const auto cpu = profiler_.Snapshot(); const auto gpu = renderer_->PollGpuProfile();
        if (csv_.is_open()) {
            const auto phys = game_->Physics(); const auto mem = game_->World().Memory();
            csv_ << std::fixed << std::setprecision(4) << frame_ << ',' << cpu.frameMs << ',' << cpu.fps << ','
                 << cpu.milliseconds[0] << ',' << cpu.milliseconds[1] << ',' << cpu.milliseconds[2] << ',' << cpu.milliseconds[3] << ','
                 << gpu.valid << ',' << gpu.sampleId << ',' << gpu.shadowMs << ',' << gpu.colorMs << ',' << gpu.totalMs << ','
                 << shadowStats.draws << ',' << stats.draws << ',' << stats.culled << ',' << mem.entities << ','
                 << phys.bodies << ',' << phys.contacts << ',' << mem.slotCapacityBytes << ',' << meshes_->Size() << '\n';
            if (!csv_) throw std::runtime_error("Profile CSV write failed");
        }
        ++frame_;
        const auto state = game_->Status();
        std::wostringstream title;
        title << L"REMI Gravity | " << (state == remi::game::State::Won ? L"FINISH! Enter to replay" : state == remi::game::State::Lost ? L"FELL! Enter to retry" : L"Collect all coins, then reach the GREEN pad")
              << L" | Gravity " << (game_->Inverted() ? L"UP" : L"DOWN") << L" | " << (game_->Supported() ? L"Space: flip ready" : L"Airborne")
              << L" | WASD move / Space flip / Q material / Enter restart / F2 debug / RMB camera / Wheel zoom / Esc";
        window.SetTitle(title.str());
    }
    void OnStop() noexcept override {
        overlay_.Clear(); hud_.reset(); game_.reset(); animation_.reset(); meshes_.reset();
        try { if (renderer_) { const auto audit = renderer_->ShutdownAndValidate(); failed = audit.liveChildren != 0 || audit.priorWarnings != 0;
            remi::Log("Game GPU shutdown: live children="+std::to_string(audit.liveChildren)+", warnings="+std::to_string(audit.priorWarnings)); } }
        catch (const std::exception& e) { failed = true; remi::Log(e.what()); }
        renderer_.reset();
        if (csv_.is_open()) { csv_.flush(); if (!csv_) failed = true; csv_.close(); }
        if (!demoDirectory.empty()) remi::Log("Gameplay demo frames captured: " + std::to_string(demoFrames_));
    }
private:
    std::unique_ptr<remi::Renderer> renderer_;
    std::unique_ptr<remi::ResourceCache<remi::Mesh>> meshes_;
    std::unique_ptr<remi::game::GravityGame> game_;
    std::unique_ptr<remi::BakedAnimation> animation_;
    std::unique_ptr<GameHud> hud_;
    remi::MeshHandle playerHandle_;
    bool running_ = false;
    remi::Camera camera_;
    remi::debug::DebugOverlay overlay_;
    remi::CpuProfiler profiler_;
    std::ofstream csv_;
    double physicsMs_ = 0, overlayAge_ = 1;
    bool showDebug_ = false;
    unsigned frame_ = 0;
    unsigned demoTick_ = 0, nextDemoCaptureTick_ = 0, demoFrames_ = 0;
};
int wmain(int argc,wchar_t** argv) {
    remi::RunConfig config; GravityApp app;
    const auto clipName = [](const wchar_t* raw) {
        std::string name;
        for (const wchar_t c : std::wstring_view(raw)) {
            if (c > 127 || !((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'_' || c == L'-') || name.size() >= 31) return std::string{};
            name.push_back(static_cast<char>(c));
        }
        return name;
    };
    config.window.title = L"REMI Gravity | WASD move | Space flip | Enter restart | F2 debug";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") app.smoke = true;
        else if (arg == L"--preview-finish") { app.smoke = true; app.previewFinish = true; }
        else if (arg == L"--preview-fell") { app.smoke = true; app.previewFell = true; }
        else if (arg == L"--warp") app.warp = true;
        else if (arg == L"--capture" && i+1 < argc) app.capture = argv[++i];
        else if (arg == L"--profile-csv" && i+1 < argc) app.profileCsv = argv[++i];
        else if (arg == L"--record-demo" && i+1 < argc) app.demoDirectory = argv[++i];
        else if (arg == L"--character" && i+1 < argc) app.characterFile = argv[++i];
        else if (arg == L"--idle-clip" && i+1 < argc) { app.idleClip = clipName(argv[++i]); if (app.idleClip.empty()) return 2; }
        else if (arg == L"--move-clip" && i+1 < argc) { app.moveClip = clipName(argv[++i]); if (app.moveClip.empty()) return 2; }
        else if (arg == L"--profile-frames" && i+1 < argc) {
            try { const auto value = std::stoul(argv[++i]); if (value < 12 || value > 10000) return 2; app.profileFrames = static_cast<unsigned>(value); app.smoke = true; }
            catch (const std::exception&) { return 2; }
        }
        else return 2;
    }
    if (app.previewFinish && app.previewFell) return 2;
    if (!app.demoDirectory.empty() && app.smoke) return 2;
    if (app.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = app.profileFrames; }
    if (!app.demoDirectory.empty()) { config.window.visible = false; config.pauseWhenInactive = false; }
    const int result = remi::RunApplication(app,config); return result ? result : (app.failed ? 1 : 0);
}
