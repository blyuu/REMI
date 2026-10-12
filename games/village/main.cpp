#include <remi/assets/StaticGltfCache.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/JobSystem.hpp>
#include <remi/debug/DebugOverlay.hpp>
#include <remi/debug/SceneInspector.hpp>
#include <remi/debug/SettingsMenu.hpp>
#include <remi/physics/PhysicsWorld.hpp>
#include <remi/render/BakedAnimation.hpp>
#include <remi/render/PrimitiveMesh.hpp>
#include <remi/render/SceneRenderSession.hpp>
#include <remi/runtime/Application.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr auto quarryFile = "build/local-assets/quarry/quarry_map.glb";
// Local compatibility probe only. Soul: City's UE-only legacy license may
// prohibit a REMI release; its export, launcher and captures stay ignored.
constexpr auto soulCityFile = "build/local-assets/soul-city/soul_city_neighborhood.glb";
constexpr auto ravenFile = "build/local-assets/raven/raven.glb";
constexpr auto survivorFile = "build/local-assets/quarry/survival_character.glb";
constexpr auto mannequinFile = "assets/Quinn/quinn.rmc";
constexpr remi::Vec3 quarrySpawn{0.f, 1.02f, -18.f};
// The level's PlayerStart sits in a tight passage. Use the central walkway
// for a readable third-person view. Epic maps UE (X,Y,Z) to glTF (X,Z,Y),
// then REMI flips glTF Z for its left-handed view.
constexpr remi::Vec3 soulCitySpawn{0.f, 1.2f, 0.f};
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

class QuarryLayer final : public remi::Layer {
public:
    fs::path mapFile, characterFile, capture;
    fs::path profileCsv;
    bool smoke = false, warp = false, failed = false, noCharacter = false, quarryMode = true, forceForward = false;
    bool culling = true;
    unsigned profileFrames = 0, warmupFrames = 120;
    bool ravenDefault = false;
    remi::Vec3 spawn_ = quarrySpawn;
    remi::Vec3 spawnOverride{};
    bool useSpawnOverride = false;
    float initialYaw = .12f, initialPitch = .32f, initialDistance = 7.f;

    void OnAttach(remi::Window& window) override {
        if (!smoke && profileCsv.empty()) window.SetRelativeMouseMode(true);
        spawn_ = useSpawnOverride ? spawnOverride : (quarryMode ? quarrySpawn : soulCitySpawn);
        if (mapFile.empty()) mapFile = FindLocalFile(quarryMode ? quarryFile : soulCityFile);
        if (mapFile.empty() || !fs::is_regular_file(mapFile))
            throw std::runtime_error(quarryMode ?
                "African Slate Quarry GLB not found. Run Play-Quarry.cmd or pass --map <file.glb>." :
                "Soul City GLB not found at build/local-assets/soul-city/soul_city_neighborhood.glb.");
        if (noCharacter) characterFile.clear();
        else if (characterFile.empty()) characterFile = FindLocalFile(quarryMode ? survivorFile : mannequinFile);
        if (!noCharacter && (characterFile.empty() || !fs::is_regular_file(characterFile)))
            throw std::runtime_error(ravenDefault ?
                "Raven GLB not found at build/local-assets/raven/raven.glb." : quarryMode ?
                "Survival character GLB not found. Run Play-Quarry.cmd or pass --character <file.glb>." :
                "Unreal Quinn mannequin not found. Export assets/Quinn/quinn.rmc or pass --character <file.rmc>.");
          remi::RendererConfig config;
          config.nativeWindow = window.NativeHandle(); config.width = window.Width(); config.height = window.Height();
          ravenCharacter_ = characterFile.filename() == L"raven.glb";
          config.deferred = ravenCharacter_ && !forceForward;
        config.shaderFile = remi::ExecutableDirectory() / "shaders/Basic.hlsl";
        config.useWarp = warp; config.vsync = !smoke && profileCsv.empty(); config.bloom = !quarryMode;
        auto session = std::make_unique<remi::SceneRenderSession>(config);
        auto jobs = std::make_unique<remi::JobSystem>();
        auto assetCache = std::make_unique<remi::assets::StaticGltfCache>(session->Device(),session->Meshes(),jobs.get());
        auto scene = std::make_unique<remi::Scene>();
        const auto mapEntities = assetCache->Instantiate(*scene,mapFile);
        for (auto id : mapEntities) scene->SetName(id,quarryMode ? "African Slate Quarry visual" : "Soul City visual");
        remi::Log(std::string(quarryMode ? "African Slate Quarry: " : "Soul City: ") +
            std::to_string(mapEntities.size()) + " scene primitives");
        if (!quarryMode && mapFile.filename() == fs::path(soulCityFile).filename()) {
            std::ifstream input(FindLocalFile("build/local-assets/soul-city/lights.csv"));
            std::string line;
            while (std::getline(input,line)) {
                std::replace(line.begin(),line.end(),',',' ');
                std::istringstream row(line);
                remi::PointLight point;
                if (row >> point.position.x >> point.position.y >> point.position.z >> point.radius
                        >> point.color.x >> point.color.y >> point.color.z >> point.intensity)
                    localLights_.push_back(point);
            }
            remi::Log("Soul City local point lights: " + std::to_string(localLights_.size()));
        }

        auto physics = std::make_unique<remi::PhysicsWorld>(*scene);
        const auto collider = [&](const char* name, remi::Vec3 position, remi::Vec3 half) {
            const auto id = scene->Create(name);
            scene->Get<remi::TransformComponent>(id)->position = position;
            remi::BodyDesc body; body.halfExtent = half;
            physics->AddBody(id,body);
        };
        // Exported Unreal geometry is visual only. These AABBs keep the local
        // prototype playable; precise building/terrain collision is future work.
        if (quarryMode) {
            collider("Quarry floor proxy",{0.f,-.35f,2.f},{26.f,.35f,33.f});
            collider("West slate bench proxy",{-10.f,3.f,2.f},{4.2f,3.f,31.f});
            collider("East slate bench proxy",{10.f,3.f,2.f},{4.2f,3.f,31.f});
            collider("Quarry far end proxy",{0.f,3.f,35.f},{10.f,3.f,4.f});
            for (const auto obstacle : {remi::Vec3{-2.8f,0.f,-6.f},remi::Vec3{2.6f,0.f,-1.f},
                                        remi::Vec3{-2.7f,0.f,6.f},remi::Vec3{2.6f,0.f,12.f},
                                        remi::Vec3{-2.6f,0.f,18.f},remi::Vec3{2.8f,0.f,24.f}})
                collider("Loose slate proxy",{obstacle.x,.55f,obstacle.z},{.75f,.55f,.75f});
        } else {
            collider("Soul City walkable floor proxy",{spawn_.x,spawn_.y-1.04f,spawn_.z},{30.f,.16f,30.f});
        }

        const auto player = scene->Create("Explorer physics body");
        scene->Get<remi::TransformComponent>(player)->position = spawn_;
        remi::BodyDesc playerBody; playerBody.type = remi::BodyType::Dynamic;
        playerBody.halfExtent = {.35f,.9f,.35f};
        physics->AddBody(player,playerBody);

        std::unique_ptr<remi::BakedAnimation> animation;
        if (!characterFile.empty() && characterFile.extension() == ".rmc") {
            animation = remi::BakedAnimation::Load(characterFile);
            if (!animation->HasClip("idle") || !animation->HasClip("run"))
                throw std::runtime_error("Character asset needs idle and run clips");
        }
        const auto visual = scene->Create(quarryMode ? "Survival character visual" : "Unreal Quinn mannequin visual");
        remi::MeshHandle characterMesh;
        std::unique_ptr<remi::assets::GltfAsset> characterAsset;
        if (animation || noCharacter) {
            characterMesh = session->Meshes().Load("quarry-character",[&] {
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
            if (characterFile.filename() == L"raven.glb") {
                remi::MaterialGraph graph;
                const auto source = graph.InputRoughness();
                const auto scale = graph.Constant({.9f,.9f,.9f});
                graph.SetOutput(remi::MaterialGraph::Output::Roughness,graph.Multiply(source,scale));
                for (std::size_t i=0; i<characterAsset->PrimitiveCount(); ++i)
                    characterAsset->ApplyMaterialGraph(i,graph);
                (void)graph.Collect();
                remi::Log("Raven material graph: imported roughness x 0.9");
            }
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
            if (advanced.z < spawn_.z+.5f || !physics->Supported(player))
                throw std::runtime_error("Exploration movement/floor smoke failed");
            physics->SetVelocity(player,{});
            remi::Log("Exploration movement/floor smoke passed");
        }

        render_ = std::move(session); renderer_ = &render_->Device();
        jobs_ = std::move(jobs); assets_ = std::move(assetCache); scene_ = std::move(scene); physics_ = std::move(physics);
        animation_ = std::move(animation); characterAsset_ = std::move(characterAsset);
        player_ = player; visual_ = visual; characterMesh_ = characterMesh;
        settings_ = std::make_unique<remi::ui::SettingsMenu>(
            remi::ExecutableDirectory()/"assets/fonts/NotoSansKR-VF.ttf",
            quarryMode ? L"REMI QUARRY" : L"REMI SOUL CITY",
            std::vector<std::wstring>{L"WASD   이동",L"Shift   달리기",L"Space   점프",
                L"마우스 이동   카메라 회전",L"마우스 휠   확대 / 축소",
                L"F1 계측  F2 컬링  F3 Inspector",L"F4 Wireframe  F6 Forward/Deferred",
                L"Esc   커서 / 종료",L"빈 곳 클릭   게임 복귀"},true);
        mouseLook_ = !smoke;
        camera_.target = {spawn_.x,spawn_.y+.4f,spawn_.z};
        camera_.yaw = initialYaw; camera_.pitch = initialPitch;
        camera_.distance = initialDistance; camera_.farPlane = 350.f;
          remi::Log(animation_ ? std::string("Exploration character: animated ") + (quarryMode ? "RMCH" : "Unreal Quinn mannequin") :
             (noCharacter ? "Exploration character: box fallback" :
                 (characterAsset_->HasSkeleton() ? "Exploration character: rigged glTF" :
                     "Exploration character: static glTF " + characterFile.filename().string())));
    }
    void OnResize(unsigned width,unsigned height) override { renderer_->Resize(width,height); }
    void OnFixedUpdate(remi::Window& window,const remi::Input& input,double dt) override {
        const auto updateStart=Clock::now();
        if (profileCsv.empty()) {
            if (input.Pressed(remi::Key::F1)) { showOverlay_=!showOverlay_; if (!showOverlay_) overlay_.Clear(); }
            if (input.Pressed(remi::Key::F2)) { culling=!culling; lastGpu_={}; overlayFrames_=0; }
            if (input.Pressed(remi::Key::F4)) {
                renderer_->SetWireframeEnabled(!renderer_->WireframeEnabled()); lastGpu_={}; overlayFrames_=0;
            }
            if (input.Pressed(remi::Key::F6)) {
                renderer_->SetDeferredEnabled(!renderer_->DeferredEnabled()); lastGpu_={}; overlayFrames_=0;
            }
        }
        const bool wasInspecting=inspector_.Visible();
        if (profileCsv.empty()) inspector_.Update(*scene_,input,static_cast<float>(dt));
        if (!wasInspecting && inspector_.Visible()) {
            mouseLook_=false; window.SetRelativeMouseMode(false);
        }
        if (inspector_.Visible()) { updateMs_+=Milliseconds(updateStart); return; }
        if (profileCsv.empty() && input.Pressed(remi::Key::Escape) && !smoke) {
            if (mouseLook_) { mouseLook_ = false; window.SetRelativeMouseMode(false); }
            else if (settings_->Open()) settings_->SetOpen(false);
            else window.RequestClose();
            updateMs_+=Milliseconds(updateStart); return;
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
            updateMs_+=Milliseconds(updateStart); return;
        }
        if (window.Focused() && profileCsv.empty())
            camera_.Orbit(-static_cast<float>(input.DeltaX())*rawMouseScale,
                static_cast<float>(input.DeltaY())*rawMouseScale);
        if (profileCsv.empty()) camera_.Zoom(input.Wheel());
        const float x = profileCsv.empty() ? float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A)) : 0.f;
        const float forward = profileCsv.empty() ? float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S)) : 0.f;
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
        if (profileCsv.empty() && input.Pressed(remi::Key::Space) && physics_->Supported(player_)) velocity.y = 5.f;
        physics_->SetVelocity(player_,velocity);
        physics_->Step(static_cast<float>(dt));
        if (moving_) scene_->Get<remi::TransformComponent>(visual_)->rotation.y =
            std::atan2(vx,vz) + (characterAsset_ ? 3.14159265f : 0.f);
        if (scene_->Get<remi::TransformComponent>(player_)->position.y < -8.f) {
            scene_->Get<remi::TransformComponent>(player_)->position = spawn_;
            physics_->SetVelocity(player_,{});
        }
        updateMs_+=Milliseconds(updateStart);
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
        remi::DirectionalLight light;
        if (quarryMode) light.ambient = .5f;
        else {
            // The source slum uses a cool night palette. Lower fill lets its
            // emissive signs and windows read against the concrete surfaces.
            light.ambient = .22f;
            light.intensity = .62f;
            light.color = {1.f,.82f,.68f};
            light.skyAmbient = {.11f,.17f,.26f};
            light.groundAmbient = {.05f,.05f,.09f};
            light.exposure = 1.15f;
            light.fogColor = {.06f,.08f,.13f};
            light.fogDensity = .006f;
        }
        light.cameraPosition = camera_.Position();
        if (ravenCharacter_) {
            light.ambient=.28f; light.intensity=.9f;
            light.color={1.f,.94f,.88f};
            light.skyAmbient={.19f,.23f,.30f};
            light.groundAmbient={.075f,.085f,.11f};
            light.exposure=1.25f;
            light.fogDensity=quarryMode ? 0.f : .003f;
            light.pointCount=3;
            const auto eye=camera_.Position();
            const remi::Vec3 facing{eye.x-position.x,0,eye.z-position.z};
            light.pointLights[0]={{eye.x,eye.y+1.f,eye.z},7.f,{1.f,.91f,.81f},8.f};
            light.pointLights[1]={{position.x-facing.x*.55f,position.y+2.8f,
                                   position.z-facing.z*.55f},6.f,{.45f,.70f,1.f},6.f};
            light.pointLights[2]={{position.x+2.1f,position.y+1.8f,position.z-1.6f},
                                   5.f,{.68f,.88f,1.f},3.f};
        }
        if (!ravenCharacter_ && !quarryMode && !localLights_.empty()) {
            std::vector<std::pair<float,std::size_t>> nearby;
            nearby.reserve(localLights_.size());
            for (std::size_t i = 0; i < localLights_.size(); ++i) {
                const auto& point = localLights_[i];
                const float dx = point.position.x-position.x;
                const float dy = point.position.y-position.y;
                const float dz = point.position.z-position.z;
                const float distanceSquared = dx*dx+dy*dy+dz*dz;
                if (distanceSquared < 4.f*point.radius*point.radius)
                    nearby.emplace_back(distanceSquared/(point.radius*point.radius),i);
            }
            std::sort(nearby.begin(),nearby.end());
            for (const auto& [distance,index] : nearby) {
                (void)distance;
                if (light.pointCount == light.pointLights.size()) break;
                light.pointLights[light.pointCount++] = localLights_[index];
            }
        }
        const auto lightVP = remi::DirectionalShadowMatrix(camera_.target,light.direction,32.f);
        const auto characterWorld = scene_->WorldMatrix(visual_);
        renderer_->BeginGpuProfile();
        const auto renderStart=Clock::now();
        const auto shadowStats=render_->DrawShadow(*scene_,lightVP,[&](remi::Renderer& device) {
            if (characterAsset_) characterAsset_->DrawShadow(device,characterWorld,lightVP);
        },culling);
        const auto stats = render_->DrawColor(*scene_,vp,light,[&](remi::Renderer& device) {
            if (characterAsset_) characterAsset_->Draw(device,characterWorld,vp,light);
        },culling);
        if (frame_ == 0) remi::Log("Exploration first frame color draws: " + std::to_string(stats.draws));
        if (!quarryMode) renderer_->ApplyPostProcess();
        const double renderMs=Milliseconds(renderStart);
        if (showOverlay_ && (!overlayFrames_++ || overlayFrames_%12==0)) {
            std::vector<std::string> lines;
            const auto line=[&](const auto& write) {
                std::ostringstream stream; stream << std::fixed << std::setprecision(2); write(stream);
                lines.push_back(stream.str());
            };
            lines.push_back("REMI ENGINE  F1 HIDE  F2 CULL  F3 INSPECT  F4 WIRE  F6 PATH");
            line([&](auto& s) { if (elapsed>0) s << "FPS " << 1.0/elapsed << " FRAME " << elapsed*1000.0 << " MS";
                else s << "FRAME TIME UNAVAILABLE"; });
            line([&](auto& s) { s << "PATH " << (renderer_->DeferredEnabled()?"DEFERRED":"FORWARD")
                << " CULL " << (culling?"ON":"OFF") << " WIRE " << (renderer_->WireframeEnabled()?"ON":"OFF"); });
            line([&](auto& s) { s << "CPU UPDATE " << updateMs_ << " MS  RENDER SUBMIT " << renderMs << " MS"; });
            line([&](auto& s) { if (lastGpu_.valid) s << "GPU SHADOW " << lastGpu_.shadowMs
                << " COLOR " << lastGpu_.colorMs << " TOTAL " << lastGpu_.totalMs << " MS";
                else s << "GPU TIMING UNAVAILABLE"; });
            line([&](auto& s) { s << "DRAWS SCENE " << stats.draws << " CHARACTER "
                << (characterAsset_ ? characterAsset_->PrimitiveCount() : 1)
                << " SHADOW " << shadowStats.draws << " CULLED " << stats.culled; });
            overlay_.Update(*renderer_,renderer_->Width(),renderer_->Height(),lines);
        }
        if (showOverlay_) overlay_.Draw(*renderer_);
        inspector_.Draw(*renderer_,*scene_);
        settings_->Update(*renderer_,renderer_->Width(),renderer_->Height());
        settings_->Draw(*renderer_);
        render_->RequireCleanDiagnostics();
        const auto gpuSampleId=renderer_->EndGpuProfile();
        if (gpuSampleId) {
            sampleModes_[gpuSampleId]={renderer_->DeferredEnabled(),culling,renderer_->WireframeEnabled()};
            while (sampleModes_.size()>16) sampleModes_.erase(sampleModes_.begin());
        }
        if (!capture.empty() && frame_ == 4) renderer_->SaveScreenshot(capture);
        renderer_->Present();
        if (!profileCsv.empty() && frame_ >= warmupFrames &&
            (!profileFrames || frame_ < warmupFrames+profileFrames)) {
            const unsigned characterDraws=characterAsset_ ? static_cast<unsigned>(characterAsset_->PrimitiveCount()) : 1u;
            ProfileRow row;
            row.frame=frame_-warmupFrames; row.route=renderer_->DeferredEnabled()?"deferred":"forward";
            row.culling=culling; row.width=renderer_->Width(); row.height=renderer_->Height();
            row.cpuUpdateMs=updateMs_; row.cpuRenderMs=renderMs;
            row.colorDraws=stats.draws+characterDraws; row.shadowDraws=shadowStats.draws+characterDraws;
            row.drawCalls=renderer_->DrawCallCount();
            row.culled=stats.culled; row.sampleId=gpuSampleId;
            if (gpuSampleId) pendingSamples_[gpuSampleId]=profileRows_.size();
            profileRows_.push_back(row);
        }
        const auto completed=renderer_->PollGpuProfile();
        if (completed.valid) {
            if (const auto mode=sampleModes_.find(completed.sampleId); mode!=sampleModes_.end() &&
                mode->second.deferred==renderer_->DeferredEnabled() && mode->second.culling==culling &&
                mode->second.wireframe==renderer_->WireframeEnabled()) lastGpu_=completed;
            for (auto it=sampleModes_.begin(); it!=sampleModes_.end() && it->first<=completed.sampleId;)
                it=sampleModes_.erase(it);
            if (const auto found=pendingSamples_.find(completed.sampleId); found!=pendingSamples_.end()) {
                profileRows_[found->second].gpu=completed;
                pendingSamples_.erase(found);
            }
        }
        updateMs_=0; ++frame_;
    }
    void OnDetach() noexcept override {
        try {
            if (!profileCsv.empty()) {
                std::ofstream csv(profileCsv,std::ios::trunc);
                if (!csv) throw std::runtime_error("Profile CSV open failed");
                csv << "frame,route,culling,width,height,cpu_update_ms,cpu_render_submit_ms,gpu_valid,gpu_sample_id,gpu_shadow_ms,gpu_color_ms,gpu_total_ms,draw_calls,color_draws,shadow_draws,color_culled\n";
                csv << std::fixed << std::setprecision(6);
                for (const auto& row:profileRows_) {
                    csv << row.frame << ',' << row.route << ',' << (row.culling?"on":"off") << ','
                        << row.width << ',' << row.height << ',' << row.cpuUpdateMs << ',' << row.cpuRenderMs
                        << ',' << row.gpu.valid << ',' << row.sampleId << ',';
                    if (row.gpu.valid) csv << row.gpu.shadowMs << ',' << row.gpu.colorMs << ',' << row.gpu.totalMs;
                    else csv << ",,";
                    csv << ',' << row.drawCalls << ',' << row.colorDraws << ',' << row.shadowDraws << ',' << row.culled << '\n';
                }
                if (!csv) throw std::runtime_error("Profile CSV write failed");
            }
        } catch (const std::exception& error) { failed=true; remi::Log(error.what()); }
        overlay_.Clear(); inspector_.Clear();
        settings_->Clear(); settings_.reset(); physics_.reset(); scene_.reset(); assets_.reset();
        characterAsset_.reset(); jobs_.reset(); animation_.reset();
        try {
            if (render_) {
                const auto audit = render_->ShutdownAndValidate();
                failed = failed || audit.liveChildren != 0 || audit.priorWarnings != 0;
            }
        } catch (const std::exception& error) { failed = true; remi::Log(error.what()); }
        render_.reset(); renderer_ = nullptr;
    }
private:
    using Clock=std::chrono::steady_clock;
    static double Milliseconds(Clock::time_point start) {
        return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    }
    struct ProfileRow {
        unsigned frame=0,width=0,height=0,drawCalls=0,colorDraws=0,shadowDraws=0,culled=0;
        const char* route="forward";
        bool culling=true;
        double cpuUpdateMs=0,cpuRenderMs=0;
        std::uint64_t sampleId=0;
        remi::GpuTimings gpu;
    };
    struct SampleMode { bool deferred=false,culling=true,wireframe=false; };
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
    remi::debug::DebugOverlay overlay_;
    remi::debug::SceneInspector inspector_;
    remi::GpuTimings lastGpu_;
    std::vector<ProfileRow> profileRows_;
    std::map<std::uint64_t,std::size_t> pendingSamples_;
    std::map<std::uint64_t,SampleMode> sampleModes_;
    double updateMs_=0;
    unsigned overlayFrames_=0;
    bool showOverlay_=false;
    unsigned frame_ = 0;
    bool moving_ = false, mouseLook_ = false;
    bool ravenCharacter_ = false;
    std::string_view activeClip_ = "idle";
    std::vector<remi::PointLight> localLights_;
};
}

int wmain(int argc,wchar_t** argv) {
    remi::RunConfig config;
    auto layer = std::make_unique<QuarryLayer>(); auto& quarry = *layer;
    config.window.title = L"REMI African Slate Quarry | Third-person exploration";
    config.window.graphicsSurface = true; config.idleWaitMilliseconds = 0;
#ifdef REMI_RAVEN_DEFAULT
    quarry.ravenDefault = true;
    quarry.quarryMode = false;
    quarry.characterFile = FindLocalFile(ravenFile);
    quarry.initialYaw = 4.3f;
    quarry.initialPitch = .1f;
    quarry.initialDistance = 3.2f;
#endif
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"--smoke") quarry.smoke = true;
        else if (arg == L"--warp") quarry.warp = true;
        else if (arg == L"--forward") quarry.forceForward = true;
        else if (arg == L"--no-culling") quarry.culling = false;
        else if (arg == L"--culling") quarry.culling = true;
        else if (arg == L"--profile-csv" && i+1 < argc) quarry.profileCsv = argv[++i];
        else if (arg == L"--profile-frames" && i+1 < argc) {
            try { quarry.profileFrames=static_cast<unsigned>(std::stoul(argv[++i])); }
            catch (const std::exception&) { return 2; }
            if (quarry.profileFrames<30 || quarry.profileFrames>10000) return 2;
        }
        else if (arg == L"--warmup-frames" && i+1 < argc) {
            try { quarry.warmupFrames=static_cast<unsigned>(std::stoul(argv[++i])); }
            catch (const std::exception&) { return 2; }
            if (quarry.warmupFrames>10000) return 2;
        }
        else if (arg == L"--map" && i+1 < argc) quarry.mapFile = argv[++i];
        else if (arg == L"--character" && i+1 < argc) quarry.characterFile = argv[++i];
        else if (arg == L"--camera-yaw" && i+1 < argc) quarry.initialYaw = std::stof(argv[++i]);
        else if (arg == L"--camera-pitch" && i+1 < argc) quarry.initialPitch = std::stof(argv[++i]);
        else if (arg == L"--camera-distance" && i+1 < argc) quarry.initialDistance = std::stof(argv[++i]);
        else if (arg == L"--spawn" && i+3 < argc) {
            quarry.spawnOverride = {std::stof(argv[++i]),std::stof(argv[++i]),std::stof(argv[++i])};
            quarry.useSpawnOverride = true;
        }
        else if (arg == L"--no-character") quarry.noCharacter = true;
        else if (arg == L"--quarry") {
            quarry.quarryMode = true;
            quarry.initialYaw = .12f; quarry.initialPitch = .32f; quarry.initialDistance = 7.f;
        }
        else if (arg == L"--soul-city") {
            quarry.quarryMode = false;
            quarry.initialYaw = -1.1f; quarry.initialPitch = .2f; quarry.initialDistance = 3.f;
        }
        else if (arg == L"--capture" && i+1 < argc) quarry.capture = argv[++i];
        else return 2;
    }
    if (!quarry.quarryMode) config.window.title = quarry.ravenDefault ?
        L"REMI Raven | Soul City" : L"REMI Soul City | Third-person exploration";
    if (!quarry.profileCsv.empty()) { config.window.width=1920; config.window.height=1080; }
    if (quarry.profileFrames && quarry.profileCsv.empty()) return 2;
    if (!quarry.profileCsv.empty() && !quarry.profileFrames) return 2;
    if (!quarry.profileCsv.empty() && (quarry.smoke || quarry.warp)) return 2;
    if (quarry.profileFrames) { config.frameLimit=quarry.warmupFrames+quarry.profileFrames; config.pauseWhenInactive=false; }
    if (quarry.smoke) { config.window.visible = false; config.pauseWhenInactive = false; config.frameLimit = 12; }
    remi::Application app; app.PushLayer(std::move(layer));
    const int result = remi::RunApplication(app,config);
    return result ? result : (quarry.failed ? 1 : 0);
}
