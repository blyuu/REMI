#include <remi/assets/StaticGltfCache.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/JobSystem.hpp>
#include <remi/debug/SettingsMenu.hpp>
#include <remi/physics/PhysicsWorld.hpp>
#include <remi/render/BakedAnimation.hpp>
#include <remi/render/PrimitiveMesh.hpp>
#include <remi/render/SceneRenderSession.hpp>
#include <remi/runtime/Application.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr auto apocalypseFile = "build/local-assets/apocalypse/apocalypse_building.glb";
constexpr auto survivorFile = "build/local-assets/apocalypse/survival_character.glb";
constexpr remi::Vec3 spawn{0.f, 1.02f, 0.f};
// Camera::Orbit uses 0.005 radians per unit for pixel-based drag controls.
// Raw mouse counts need a lower scale; 100 counts now turn about 5.7 degrees.
constexpr float rawMouseScale = .2f;

fs::path FindLocalFile(const fs::path& relative) {
    for (auto base : {fs::current_path(), remi::ExecutableDirectory()}) {
        for (unsigned level = 0; level < 6; ++level) {
            const auto candidate = base / relative;
            if (fs::is_regular_file(candidate)) return fs::canonical(candidate);
            if (base == base.parent_path()) break;
            base = base.parent_path();
        }
    }
    return {};
}

class VillageLayer final : public remi::Layer {
public:
    fs::path mapFile, characterFile, capture;
    bool smoke = false, warp = false, failed = false, noCharacter = false;

    void OnAttach(remi::Window& window) override {
        if (!smoke) window.SetRelativeMouseMode(true);
        if (mapFile.empty()) mapFile = FindLocalFile(apocalypseFile);
        if (mapFile.empty() || !fs::is_regular_file(mapFile))
            throw std::runtime_error("Apocalypse building GLB not found. Run the Blender asset builder or pass --map <file.glb>.");
        if (noCharacter) characterFile.clear();
        else if (characterFile.empty()) characterFile = FindLocalFile(survivorFile);
        if (!noCharacter && (characterFile.empty() || !fs::is_regular_file(characterFile)))
            throw std::runtime_error("Survival character GLB not found. Run the Blender asset builder or pass --character <file.glb>.");
        remi::RendererConfig config;
        config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
        config.shaderFile = remi::ExecutableDirectory() / "shaders/Basic.hlsl";
        config.useWarp = warp; config.vsync = !smoke;
        auto session = std::make_unique<remi::SceneRenderSession>(config);
        auto jobs = std::make_unique<remi::JobSystem>();
        auto assetCache = std::make_unique<remi::assets::StaticGltfCache>(session->Device(),session->Meshes(),jobs.get());
        auto scene = std::make_unique<remi::Scene>();
        const std::array buildingSites{
            remi::Vec3{-16.f,0.f,-18.f}, remi::Vec3{16.f,0.f,-18.f},
            remi::Vec3{-16.f,0.f,18.f}, remi::Vec3{16.f,0.f,18.f}};
        std::size_t buildingPrimitives = 0;
        for (std::size_t i = 0; i < buildingSites.size(); ++i) {
            const auto entities = assetCache->Instantiate(*scene,mapFile);
            buildingPrimitives += entities.size();
            for (auto id : entities) {
                scene->Get<remi::TransformComponent>(id)->position = buildingSites[i];
                scene->SetName(id,"Apocalypse building " + std::to_string(i + 1));
            }
        }
        remi::Log("Apocalypse map: " + std::to_string(buildingSites.size()) +
            " shared building instances, " + std::to_string(buildingPrimitives) + " scene primitives");

        const auto groundMesh = session->Meshes().Load("apocalypse-ground",[&] {
            return remi::CreateBoxMesh(session->Device(),{.30f,.28f,.24f});
        });
        const auto roadMesh = session->Meshes().Load("apocalypse-road",[&] {
            return remi::CreateBoxMesh(session->Device(),{.13f,.15f,.17f});
        });
        const auto stripeMesh = session->Meshes().Load("apocalypse-road-stripe",[&] {
            return remi::CreateBoxMesh(session->Device(),{.73f,.60f,.31f});
        });
        const auto block = [&](const char* name, remi::Vec3 position, remi::Vec3 scale, remi::MeshHandle mesh) {
            const auto id = scene->Create(name);
            *scene->Get<remi::TransformComponent>(id) = {position,{},scale};
            scene->Add<remi::MeshComponent>(id,{mesh});
        };
        block("Apocalypse ground",{0.f,-.35f,0.f},{72.f,.7f,72.f},groundMesh);
        block("East-west road",{0.f,.012f,0.f},{72.f,.025f,8.f},roadMesh);
        block("North-south road",{0.f,.025f,0.f},{8.f,.025f,72.f},roadMesh);
        for (int i = -5; i <= 5; ++i) {
            if (i == 0) continue; // Leave the intersection clear.
            block("Broken lane marking",{0.f,.043f,static_cast<float>(i)*5.f},
                {.11f,.012f,2.2f},stripeMesh);
        }

        auto physics = std::make_unique<remi::PhysicsWorld>(*scene);
        const auto collider = [&](const char* name, remi::Vec3 position, remi::Vec3 half) {
            const auto id = scene->Create(name);
            scene->Get<remi::TransformComponent>(id)->position = position;
            remi::BodyDesc body; body.halfExtent = half;
            physics->AddBody(id,body);
        };
        // The kit is a visual building, not collision geometry. Match its four
        // placed footprints with coarse AABBs and keep the road playable.
        collider("Apocalypse ground proxy",{0.f,-.35f,0.f},{36.f,.35f,36.f});
        for (std::size_t i = 0; i < buildingSites.size(); ++i)
            collider("Building footprint proxy",{buildingSites[i].x,8.f,buildingSites[i].z},{11.5f,8.f,9.f});

        const auto player = scene->Create("Explorer physics body");
        scene->Get<remi::TransformComponent>(player)->position = spawn;
        remi::BodyDesc playerBody; playerBody.type = remi::BodyType::Dynamic;
        playerBody.halfExtent = {.35f,.9f,.35f};
        physics->AddBody(player,playerBody);

        std::unique_ptr<remi::BakedAnimation> animation;
        if (!characterFile.empty() && characterFile.extension() == ".rmc") {
            animation = remi::BakedAnimation::Load(characterFile);
            if (!animation->HasClip("idle") || !animation->HasClip("run"))
                throw std::runtime_error("Character asset needs idle and run clips");
        }
        const auto visual = scene->Create("Survival character visual");
        remi::MeshHandle characterMesh;
        std::unique_ptr<remi::assets::GltfAsset> characterAsset;
        if (animation || noCharacter) {
            characterMesh = session->Meshes().Load("apocalypse-character",[&] {
                return animation ? animation->CreateMesh(session->Device()) :
                    remi::CreateBoxMesh(session->Device(),{.20f,.78f,.88f});
            });
            scene->Get<remi::TransformComponent>(visual)->scale =
                animation ? remi::Vec3{1,1,1} : remi::Vec3{.35f,.9f,.35f};
            scene->Add<remi::MeshComponent>(visual,{characterMesh});
        } else {
            if (characterFile.extension() != ".glb" && characterFile.extension() != ".gltf")
                throw std::invalid_argument("Character must be a .glb/.gltf or an animated .rmc");
            scene->Get<remi::TransformComponent>(visual)->position.y = -.9f;
            characterAsset = remi::assets::GltfAsset::Load(session->Device(),characterFile,jobs.get());
            if (characterAsset->HasSkeleton() &&
                (!characterAsset->SetClip("idle",0) || !characterAsset->SetClip("run",0) ||
                 !characterAsset->SetClip("idle",0)))
                throw std::runtime_error("Survival character needs idle and run clips");
            if (smoke && characterAsset->HasSkeleton()) {
                (void)characterAsset->SetClip("run",0);
                characterAsset->Update(session->Device(),.2f);
                (void)characterAsset->SetClip("idle",0);
            }
            scene->Get<remi::TransformComponent>(visual)->rotation.y = 3.14159265f;
        }
        scene->SetParent(visual,player);
        physics->Step(1.f/60.f);
        if (smoke) {
            for (unsigned i = 0; i < 10; ++i) {
                auto velocity = physics->Velocity(player);
                velocity.z = 4.f;
                physics->SetVelocity(player,velocity);
                physics->Step(1.f/60.f);
            }
            const auto advanced = scene->Get<remi::TransformComponent>(player)->position;
            if (advanced.z < spawn.z+.5f || !physics->Supported(player))
                throw std::runtime_error("Village movement/floor smoke failed");
            physics->SetVelocity(player,{});
            remi::Log("Apocalypse movement/floor smoke passed");
        }

        render_ = std::move(session); renderer_ = &render_->Device();
        jobs_ = std::move(jobs); assets_ = std::move(assetCache); scene_ = std::move(scene); physics_ = std::move(physics);
        animation_ = std::move(animation); characterAsset_ = std::move(characterAsset);
        player_ = player; visual_ = visual; characterMesh_ = characterMesh;
        settings_ = std::make_unique<remi::ui::SettingsMenu>(
            remi::ExecutableDirectory()/"assets/fonts/NotoSansKR-VF.ttf",L"REMI APOCALYPSE",
            std::vector<std::wstring>{L"WASD   이동",L"Shift   달리기",L"Space   점프",
                L"마우스 이동   카메라 회전",L"마우스 휠   확대 / 축소",
                L"Esc   커서 / 종료",L"빈 곳 클릭   게임 복귀"},true);
        mouseLook_ = !smoke;
        camera_.target = {spawn.x,spawn.y+.4f,spawn.z};
        camera_.yaw = .12f; camera_.pitch = .32f; camera_.distance = 7.f; camera_.farPlane = 350.f;
        remi::Log(animation_ ? "Apocalypse character: animated RMCH" :
            (noCharacter ? "Apocalypse character: box fallback" :
                (characterAsset_->HasSkeleton() ? "Apocalypse character: rigged Survival Character" :
                    "Apocalypse character: static Survival Character")));
    }
    void OnResize(unsigned width,unsigned height) override { renderer_->Resize(width,height); }
    void OnFixedUpdate(remi::Window& window,const remi::Input& input,double dt) override {
        if (input.Pressed(remi::Key::Escape) && !smoke) {
            if (mouseLook_) { mouseLook_ = false; window.SetRelativeMouseMode(false); }
            else if (settings_->Open()) settings_->SetOpen(false);
            else window.RequestClose();
            return;
        }
        if (!mouseLook_) {
            const bool clickedSettings = settings_->HandleClick(input,window.Width(),window.Height());
            if (!smoke && input.Pressed(remi::Key::MouseLeft) && !clickedSettings && !settings_->Open()) {
                mouseLook_ = true;
                window.SetRelativeMouseMode(true);
            }
            auto velocity = physics_->Velocity(player_);
            velocity.x = velocity.z = 0;
            physics_->SetVelocity(player_,velocity);
            physics_->Step(static_cast<float>(dt));
            moving_ = false;
            return;
        }
        if (window.Focused())
            camera_.Orbit(-static_cast<float>(input.DeltaX())*rawMouseScale,
                static_cast<float>(input.DeltaY())*rawMouseScale);
        camera_.Zoom(input.Wheel());
        const float x = float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A));
        const float forward = float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S));
        const float length = std::sqrt(x*x+forward*forward);
        moving_ = length > .01f;
        const float speed = input.Held(remi::Key::Shift) ? 7.f : 4.f;
        const float scale = length > 1.f ? 1.f/length : 1.f;
        // Camera forward on the XZ plane is {-sin(yaw), +cos(yaw)}.
        const float s = std::sin(camera_.yaw), c = std::cos(camera_.yaw);
        const float vx = (x*c-forward*s)*speed*scale;
        const float vz = (x*s+forward*c)*speed*scale;
        auto velocity = physics_->Velocity(player_);
        velocity.x = vx; velocity.z = vz;
        if (input.Pressed(remi::Key::Space) && physics_->Supported(player_)) velocity.y = 5.f;
        physics_->SetVelocity(player_,velocity);
        physics_->Step(static_cast<float>(dt));
        if (moving_) scene_->Get<remi::TransformComponent>(visual_)->rotation.y =
            std::atan2(vx,vz) + (characterAsset_ ? 3.14159265f : 0.f);
        if (scene_->Get<remi::TransformComponent>(player_)->position.y < -8.f) {
            scene_->Get<remi::TransformComponent>(player_)->position = spawn;
            physics_->SetVelocity(player_,{});
        }
    }
    void OnFrame(remi::Window&,double elapsed,double) override {
        const auto position = scene_->Get<remi::TransformComponent>(player_)->position;
        const float blend = 1.f-std::exp(-8.f*static_cast<float>(elapsed));
        const remi::Vec3 desired{position.x,position.y+.4f,position.z};
        camera_.target.x += (desired.x-camera_.target.x)*blend;
        camera_.target.y += (desired.y-camera_.target.y)*blend;
        camera_.target.z += (desired.z-camera_.target.z)*blend;
        if (animation_) animation_->Update(*renderer_,*render_->Meshes().GetMutable(characterMesh_),
            moving_ ? "run" : "idle",elapsed);
        if (characterAsset_ && characterAsset_->HasSkeleton()) {
            const std::string_view clip = moving_ ? "run" : "idle";
            if (activeClip_ != clip) {
                (void)characterAsset_->SetClip(clip,.15f);
                activeClip_ = clip;
            }
            characterAsset_->Update(*renderer_,static_cast<float>(elapsed));
        }
        const auto vp = camera_.ViewProjection(float(renderer_->Width())/float(renderer_->Height()));
        remi::DirectionalLight light; light.ambient = .5f; light.cameraPosition = camera_.Position();
        const auto lightVP = remi::DirectionalShadowMatrix(camera_.target,light.direction,32.f);
        const auto characterWorld = scene_->WorldMatrix(visual_);
        (void)render_->DrawShadow(*scene_,lightVP,[&](remi::Renderer& device) {
            if (characterAsset_) characterAsset_->DrawShadow(device,characterWorld,lightVP);
        });
        const auto stats = render_->DrawColor(*scene_,vp,light,[&](remi::Renderer& device) {
            if (characterAsset_) characterAsset_->Draw(device,characterWorld,vp,light);
        });
        if (frame_ == 0) remi::Log("Apocalypse first frame color draws: " + std::to_string(stats.draws));
        settings_->Update(*renderer_,renderer_->Width(),renderer_->Height());
        settings_->Draw(*renderer_);
        render_->RequireCleanDiagnostics();
        if (!capture.empty() && frame_ == 4) renderer_->SaveScreenshot(capture);
        renderer_->Present(); ++frame_;
    }
    void OnDetach() noexcept override {
        settings_->Clear(); settings_.reset(); physics_.reset(); scene_.reset(); assets_.reset();
        characterAsset_.reset(); jobs_.reset(); animation_.reset();
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
    remi::Renderer* renderer_ = nullptr;
    std::unique_ptr<remi::assets::StaticGltfCache> assets_;
    std::unique_ptr<remi::JobSystem> jobs_;
    std::unique_ptr<remi::Scene> scene_;
    std::unique_ptr<remi::PhysicsWorld> physics_;
    std::unique_ptr<remi::BakedAnimation> animation_;
    std::unique_ptr<remi::assets::GltfAsset> characterAsset_;
    remi::EntityId player_, visual_;
    remi::MeshHandle characterMesh_;
    remi::Camera camera_;
    std::unique_ptr<remi::ui::SettingsMenu> settings_;
    unsigned frame_ = 0;
    bool moving_ = false, mouseLook_ = false;
    std::string_view activeClip_ = "idle";
};
}

int wmain(int argc,wchar_t** argv) {
    remi::RunConfig config;
    auto layer = std::make_unique<VillageLayer>(); auto& village = *layer;
    config.window.title = L"REMI Apocalypse | Third-person exploration";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") village.smoke = true;
        else if (arg == L"--warp") village.warp = true;
        else if (arg == L"--map" && i+1 < argc) village.mapFile = argv[++i];
        else if (arg == L"--character" && i+1 < argc) village.characterFile = argv[++i];
        else if (arg == L"--no-character") village.noCharacter = true;
        else if (arg == L"--capture" && i+1 < argc) village.capture = argv[++i];
        else return 2;
    }
    if (village.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 12; }
    remi::Application app; app.PushLayer(std::move(layer));
    const int result = remi::RunApplication(app,config);
    return result ? result : (village.failed ? 1 : 0);
}
