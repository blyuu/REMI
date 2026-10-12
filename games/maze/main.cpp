#include "MazeHud.hpp"
#include <remi/assets/StaticGltfCache.hpp>
#include <remi/core/JobSystem.hpp>
#include <remi/core/Log.hpp>
#include <remi/debug/SceneInspector.hpp>
#include <remi/physics/PhysicsWorld.hpp>
#include <remi/render/BakedAnimation.hpp>
#include <remi/render/Camera.hpp>
#include <remi/render/SceneRenderSession.hpp>
#include <remi/runtime/Application.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr int gridSize = 13;
constexpr float cell = 4.8f;
constexpr float tau = 6.28318530718f;
constexpr float mouseScale = .2f; // Same raw-input scale as REMIVillage.
constexpr auto assetDirectory = "build/local-assets/ancient-ruins";
constexpr auto mannequinFile = "assets/Quinn/quinn.rmc";

fs::path FindLocal(const fs::path& relative) {
    for (auto base : {fs::current_path(), remi::ExecutableDirectory()}) {
        for (int i = 0; i < 7; ++i) {
            if (fs::is_regular_file(base / relative)) return fs::canonical(base / relative);
            if (base == base.parent_path()) break;
            base = base.parent_path();
        }
    }
    return {};
}

float World(int index) { return (static_cast<float>(index) - 6.f) * cell; }

// A fixed seed gives every reviewer the same walkable maze and exit location.
std::array<std::array<bool,gridSize>,gridSize> MakeMaze() {
    std::array<std::array<bool,gridSize>,gridSize> open{};
    std::mt19937 random(0x52454d49u);
    std::vector<std::pair<int,int>> stack{{1,1}};
    open[1][1] = true;
    while (!stack.empty()) {
        const auto [x,z] = stack.back();
        std::array<std::pair<int,int>,4> choices{};
        int count = 0;
        for (const auto [dx,dz] : std::array<std::pair<int,int>,4>{{{2,0},{-2,0},{0,2},{0,-2}}}) {
            const int nx=x+dx, nz=z+dz;
            if (nx>0 && nx<gridSize-1 && nz>0 && nz<gridSize-1 && !open[nz][nx])
                choices[count++]={dx,dz};
        }
        if (!count) { stack.pop_back(); continue; }
        const auto [dx,dz]=choices[random()%static_cast<unsigned>(count)];
        open[z+dz/2][x+dx/2]=true;
        open[z+dz][x+dx]=true;
        stack.emplace_back(x+dx,z+dz);
    }
    // Entrance courtyard and the final gate are part of the navigable route.
    open[0][1]=true; open[12][11]=true;
    return open;
}

std::uint32_t StoneHash(int x,int z) {
    std::uint32_t value=static_cast<std::uint32_t>(x)*0x9e3779b9u ^
        static_cast<std::uint32_t>(z)*0x85ebca6bu ^ 0x52454d49u;
    value^=value>>16; value*=0x7feb352du; value^=value>>15;
    value*=0x846ca68bu; return value^(value>>16);
}
float StoneNoise(int x,int z) { return static_cast<float>(StoneHash(x,z)&65535u)/65535.f; }
float SmoothNoise(float x,float z,int period) {
    const int ix=static_cast<int>(std::floor(x)), iz=static_cast<int>(std::floor(z));
    const float fx=x-static_cast<float>(ix), fz=z-static_cast<float>(iz);
    const auto sample=[&](int sx,int sz) {
        return StoneNoise(sx%period,sz%period);
    };
    const float ax=fx*fx*(3.f-2.f*fx), az=fz*fz*(3.f-2.f*fz);
    const float a=std::lerp(sample(ix,iz),sample(ix+1,iz),ax);
    const float b=std::lerp(sample(ix,iz+1),sample(ix+1,iz+1),ax);
    return std::lerp(a,b,az);
}

void ApplyStoneSurface(remi::Renderer& renderer,remi::Mesh& mesh) {
    constexpr unsigned size=256;
    std::vector<std::uint8_t> color(size*size*4),normal(size*size*4);
    std::vector<float> height(size*size);
    for (unsigned z=0; z<size; ++z) for (unsigned x=0; x<size; ++x) {
        const float u=static_cast<float>(x)/size, v=static_cast<float>(z)/size;
        const float broad=SmoothNoise(u*8.f,v*8.f,8);
        const float grain=SmoothNoise(u*64.f,v*64.f,64);
        const float fine=SmoothNoise(u*128.f,v*128.f,128);
        const float pore=fine<.19f ? -.13f : 0.f;
        height[z*size+x]=.66f*broad+.24f*grain+.10f*fine+pore;
        const float shade=std::clamp(.70f+.37f*(broad-.5f)+.17f*(grain-.5f)+pore,.42f,.94f);
        const float moss=std::clamp((broad-.68f)*.8f,0.f,.18f);
        const auto byte=[](float value) { return static_cast<std::uint8_t>(
            std::clamp(value*255.f,0.f,255.f)); };
        const auto index=(z*size+x)*4;
        color[index+0]=byte(shade*.91f-moss*.5f);
        color[index+1]=byte(shade*.89f+moss*.24f);
        color[index+2]=byte(shade*.81f-moss*.38f);
        color[index+3]=255;
    }
    for (unsigned z=0; z<size; ++z) for (unsigned x=0; x<size; ++x) {
        const float dx=height[z*size+(x+1)%size]-height[z*size+(x+size-1)%size];
        const float dz=height[((z+1)%size)*size+x]-height[((z+size-1)%size)*size+x];
        const float scale=2.f/std::sqrt(dx*dx*3.f+dz*dz*3.f+4.f);
        const auto byte=[](float value) { return static_cast<std::uint8_t>(
            std::clamp(value*255.f,0.f,255.f)); };
        const auto index=(z*size+x)*4;
        normal[index+0]=byte(.5f-dx*scale*.5f);
        normal[index+1]=byte(.5f-dz*scale*.5f);
        normal[index+2]=byte(scale);
        normal[index+3]=255;
    }
    renderer.SetMeshTexture(mesh,size,size,color);
    renderer.SetMeshNormalTexture(mesh,size,size,normal);
}

std::unique_ptr<remi::Mesh> MakeFlagstones(remi::Renderer& renderer) {
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(gridSize*gridSize*16*8*3);
    indices.reserve(gridSize*gridSize*16*8*3);
    const auto triangle=[&](remi::Vec3 a,remi::Vec3 b,remi::Vec3 c,
                            remi::Vec3 tint,remi::Vec2 uvA,remi::Vec2 uvB,remi::Vec2 uvC) {
        const remi::Vec3 ab{b.x-a.x,b.y-a.y,b.z-a.z};
        const remi::Vec3 ac{c.x-a.x,c.y-a.y,c.z-a.z};
        remi::Vec3 n{ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
        const float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if (length>1e-5f) { n.x/=length; n.y/=length; n.z/=length; }
        const auto first=static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({a,tint,n,uvA}); vertices.push_back({b,tint,n,uvB});
        vertices.push_back({c,tint,n,uvC});
        indices.insert(indices.end(),{first,first+1,first+2});
    };
    const float edge=gridSize*cell*.5f;
    const remi::Vec3 under{.27f,.25f,.22f};
    triangle({-edge,-.09f,-edge},{edge,-.09f,edge},{edge,-.09f,-edge},under,{},{},{});
    triangle({-edge,-.09f,-edge},{-edge,-.09f,edge},{edge,-.09f,edge},under,{},{},{});
    constexpr float width=cell*.25f;
    for (int z=0; z<gridSize*4; ++z) for (int x=0; x<gridSize*4; ++x) {
        const float px=-edge+static_cast<float>(x)*width;
        const float pz=-edge+static_cast<float>(z)*width;
        const float variation=StoneNoise(x,z);
        if (variation<.045f) continue; // A few missing slabs reveal dark rubble beneath.
        const float gap=.025f+.035f*variation;
        const float shade=.83f+.16f*(StoneNoise(x+91,z+27)-.5f);
        const remi::Vec3 tint{shade,shade*.98f,shade*.94f};
        std::array<std::array<remi::Vec3,3>,3> points{};
        for (int vz=0; vz<3; ++vz) for (int vx=0; vx<3; ++vx) {
            const float u=static_cast<float>(vx)*.5f, v=static_cast<float>(vz)*.5f;
            const float chip=(vx==1 && vz==1)?0.f:
                .08f*(StoneNoise(x*3+vx,z*3+vz)-.5f);
            const float bump=.018f+.075f*StoneNoise(x*3+vx+37,z*3+vz+51);
            points[vz][vx]={px+gap+u*(width-2.f*gap)+chip,bump,
                pz+gap+v*(width-2.f*gap)+chip};
        }
        for (int vz=0; vz<2; ++vz) for (int vx=0; vx<2; ++vx) {
            const float u=static_cast<float>(vx)*.5f, v=static_cast<float>(vz)*.5f;
            triangle(points[vz][vx],points[vz+1][vx+1],points[vz][vx+1],
                tint,{u,v},{u+.5f,v+.5f},{u+.5f,v});
            triangle(points[vz][vx],points[vz+1][vx],points[vz+1][vx+1],
                tint,{u,v},{u,v+.5f},{u+.5f,v+.5f});
        }
    }
    auto mesh=renderer.CreateMesh(vertices,indices);
    ApplyStoneSurface(renderer,*mesh);
    return mesh;
}

std::unique_ptr<remi::Mesh> MakeSky(remi::Renderer& renderer) {
    // Inward-facing hemisphere: cloudy zenith fades to a warmer, hazy horizon.
    constexpr int rings=8, sectors=32;
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int r=0; r<=rings; ++r) {
        const float elevation=1.570796327f*static_cast<float>(r)/rings;
        const float horizon=1.f-static_cast<float>(r)/rings;
        for (int s=0; s<=sectors; ++s) {
            const float angle=tau*static_cast<float>(s)/sectors;
            const float cloud=.045f*std::sin(angle*5.f+elevation*3.f)*
                std::sin(angle*9.f-elevation*7.f);
            const remi::Vec3 color{.50f+.13f*horizon+cloud,
                .57f+.10f*horizon+cloud,.65f+.05f*horizon+cloud};
            const remi::Vec3 point{120.f*std::cos(elevation)*std::cos(angle),
                120.f*std::sin(elevation)-1.f,120.f*std::cos(elevation)*std::sin(angle)};
            vertices.push_back({point,color,{0,-1,0},{}});
        }
    }
    for (int r=0; r<rings; ++r) for (int s=0; s<sectors; ++s) {
        const auto a=static_cast<std::uint32_t>(r*(sectors+1)+s);
        indices.insert(indices.end(),{a,a+1,a+sectors+1,a+1,a+sectors+2,a+sectors+1});
    }
    return renderer.CreateMesh(vertices,indices);
}

std::unique_ptr<remi::Mesh> MakeGateGlow(remi::Renderer& renderer) {
    // A narrow emissive lintel and two runes, not a substitute for the ruin geometry.
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const auto bar=[&](float x0,float y0,float x1,float y1) {
        const auto base=static_cast<std::uint32_t>(vertices.size());
        constexpr remi::Vec3 blue{.13f,.78f,1.f};
        vertices.insert(vertices.end(),{{{x0,y0,0},blue,{0,0,-1},{}},
            {{x1,y0,0},blue,{0,0,-1},{}},{{x1,y1,0},blue,{0,0,-1},{}},
            {{x0,y1,0},blue,{0,0,-1},{}}});
        indices.insert(indices.end(),{base,base+2,base+1,base,base+3,base+2});
    };
    bar(-1.3f,.1f,-1.1f,3.2f); bar(1.1f,.1f,1.3f,3.2f);
    bar(-1.3f,3.05f,1.3f,3.25f);
    return renderer.CreateMesh(vertices,indices);
}

class MazeLayer final : public remi::Layer {
public:
    void OnAttach(remi::Window& window) override {
        remi::RendererConfig config;
        config.nativeWindow=window.NativeHandle(); config.width=window.Width(); config.height=window.Height();
        config.shaderFile=remi::ExecutableDirectory()/"shaders/Basic.hlsl";
        config.bloom=true;
        render_=std::make_unique<remi::SceneRenderSession>(config);
        jobs_=std::make_unique<remi::JobSystem>();
        assets_=std::make_unique<remi::assets::StaticGltfCache>(render_->Device(),render_->Meshes(),jobs_.get());
        scene_=std::make_unique<remi::Scene>();
        physics_=std::make_unique<remi::PhysicsWorld>(*scene_);
        grid_=MakeMaze();
        const std::string variants="ABCDEFGH";
        // Source GLBs are centered at the origin but have different Y spans.
        // Scale each visual to a similar 3.6 m wall height while colliders stay fixed.
        constexpr std::array<float,8> halfHeights{.2586f,.4297f,.4933f,.4197f,
            .3466f,.3310f,.3576f,.2728f};
        for (int z=0; z<gridSize; ++z) for (int x=0; x<gridSize; ++x) {
            if (grid_[z][x]) continue;
            // Frame the entrance with matching arches and the exit with heavier ruins.
            const char variant=(z==0 && (x==0 || x==2)) ? 'A' :
                (z==12 && (x==10 || x==12)) ? 'D' :
                variants[static_cast<std::size_t>((x*7+z*11+x*z)%variants.size())];
            const fs::path relative=fs::path(assetDirectory)/
                (std::string("ruin_")+static_cast<char>(variant+('a'-'A'))+".glb");
            const auto file=FindLocal(relative);
            if (file.empty()) throw std::runtime_error(
                "Prepared Ancient Ruins GLBs are missing. Run tools/assets/prepare_ancient_ruins.py with Blender first.");
            const auto pieces=assets_->Instantiate(*scene_,file);
            for (auto id:pieces) {
                scene_->SetName(id,std::string("Ancient Ruins ")+variant);
                auto* transform=scene_->Get<remi::TransformComponent>(id);
                transform->position={World(x),1.8f,World(z)};
                transform->rotation.y=static_cast<float>((x+3*z)%4)*1.570796327f;
                transform->scale={4.4f,1.8f/halfHeights[static_cast<unsigned>(variant-'A')],4.4f};
            }
            AddCollider("Ruined masonry",{World(x),1.8f,World(z)},
                {cell*.5f,2.1f,cell*.5f});
        }
        // Larger silhouettes beyond the play grid create a continuous ruin horizon.
        const auto horizonFile=FindLocal(fs::path(assetDirectory)/"ruin_d.glb");
        for (int i=0; i<12; ++i) {
            const float angle=tau*static_cast<float>(i)/12.f;
            const auto horizon=assets_->Instantiate(*scene_,horizonFile);
            for (auto id:horizon) {
                scene_->SetName(id,"Ancient Ruins distant skyline");
                auto* transform=scene_->Get<remi::TransformComponent>(id);
                transform->position={56.f*std::cos(angle),8.4f,56.f*std::sin(angle)};
                transform->rotation.y=angle;
                transform->scale={20.f,20.f,20.f};
            }
        }
        auto floor=render_->Meshes().Load("maze:flagstone-floor",[&] { return MakeFlagstones(render_->Device()); });
        const auto floorId=scene_->Create("Stone flagstone floor");
        remi::MeshComponent floorComponent; floorComponent.mesh=floor;
        floorComponent.material.shadingModel=remi::ShadingModel::PBR;
        floorComponent.material.roughness=.92f;
        scene_->Add<remi::MeshComponent>(floorId,floorComponent);
        AddCollider("Ground",{0,-.22f,0},{gridSize*cell*.5f,.2f,gridSize*cell*.5f});
        const float edge=gridSize*cell*.5f;
        AddCollider("North boundary",{0,2.f,-edge},{edge,2.5f,.2f});
        AddCollider("South boundary",{0,2.f,edge},{edge,2.5f,.2f});
        AddCollider("West boundary",{-edge,2.f,0},{.2f,2.5f,edge});
        AddCollider("East boundary",{edge,2.f,0},{.2f,2.5f,edge});
        const auto glow=render_->Meshes().Load("maze:exit-glow",[&] { return MakeGateGlow(render_->Device()); });
        const auto gate=scene_->Create("Ancient luminous exit");
        scene_->Get<remi::TransformComponent>(gate)->position=Exit();
        remi::MeshComponent gateComponent; gateComponent.mesh=glow;
        gateComponent.material.shadingModel=remi::ShadingModel::Unlit;
        gateComponent.material.emissiveFactor={.1f,1.2f,2.f};
        scene_->Add<remi::MeshComponent>(gate,gateComponent);
        sky_=MakeSky(render_->Device());
        player_=scene_->Create("Explorer collision capsule proxy");
        scene_->Get<remi::TransformComponent>(player_)->position=Spawn();
        remi::BodyDesc body; body.type=remi::BodyType::Dynamic;
        body.halfExtent={.35f,.9f,.35f};
        physics_->AddBody(player_,body);
        const auto mannequin=FindLocal(mannequinFile);
        if (mannequin.empty()) throw std::runtime_error(
            "UE5 Quinn mannequin is missing: assets/Quinn/quinn.rmc");
        animation_=remi::BakedAnimation::Load(mannequin);
        if (!animation_->HasClip("idle") || !animation_->HasClip("run"))
            throw std::runtime_error("UE5 Quinn mannequin needs idle and run clips");
        characterMesh_=render_->Meshes().Load("maze:ue5-quinn",[&] {
            return animation_->CreateMesh(render_->Device());
        });
        visual_=scene_->Create("UE5 Quinn mannequin visual");
        // The baked Quinn idle pose spans local Y -0.50..1.07; align feet to stone.
        scene_->Get<remi::TransformComponent>(visual_)->position.y=-.4f;
        scene_->Add<remi::MeshComponent>(visual_,{characterMesh_});
        if (!scene_->SetParent(visual_,player_))
            throw std::runtime_error("Cannot attach Quinn to maze player");
        hud_=std::make_unique<MazeHud>(FindLocal("assets/fonts/NotoSansKR-VF.ttf"));
        camera_.yaw=0; camera_.pitch=.28f; camera_.distance=preferredDistance_;
        camera_.farPlane=160.f;
        window.SetRelativeMouseMode(true);
        mouseLocked_=true;
        remi::Log("Ancient Ruins maze: local LOD pieces, shared glTF cache, fixed layout");
    }
    void OnResize(unsigned width,unsigned height) override { render_->Device().Resize(width,height); }
    void OnFixedUpdate(remi::Window& window,const remi::Input& input,double seconds) override {
        if (input.Pressed(remi::Key::F1) && !cleared_) culling_=!culling_;
        if (input.Pressed(remi::Key::F2) && !cleared_) {
            panelVisible_=!panelVisible_; uiFrames_=0;
        }
        if (input.Pressed(remi::Key::F4) && !cleared_)
            render_->Device().SetWireframeEnabled(!render_->Device().WireframeEnabled());
        if (input.Pressed(remi::Key::F6) && !cleared_)
            render_->Device().SetDeferredEnabled(!render_->Device().DeferredEnabled());
        const bool inspected=inspector_.Visible();
        if (!cleared_) inspector_.Update(*scene_,input,static_cast<float>(seconds));
        if (!inspected && inspector_.Visible()) {
            mouseLocked_=false; window.SetRelativeMouseMode(false);
        }
        if (inspector_.Visible()) { moving_=false; return; }
        if (cleared_) {
            if (input.Pressed(remi::Key::Left) || input.Pressed(remi::Key::A)) quitSelected_=false;
            if (input.Pressed(remi::Key::Right) || input.Pressed(remi::Key::D)) quitSelected_=true;
            MazeHudChoice choice=MazeHudChoice::None;
            if (input.Pressed(remi::Key::MouseLeft))
                choice=MazeHud::HitTest(window.Width(),window.Height(),input.MouseX(),input.MouseY());
            if (input.Pressed(remi::Key::Enter) || input.Pressed(remi::Key::Space))
                choice=quitSelected_?MazeHudChoice::Quit:MazeHudChoice::Retry;
            if (input.Pressed(remi::Key::Escape)) choice=MazeHudChoice::Quit;
            if (choice==MazeHudChoice::Quit) window.RequestClose();
            else if (choice==MazeHudChoice::Retry) {
                scene_->Get<remi::TransformComponent>(player_)->position=Spawn();
                physics_->SetVelocity(player_,{});
                camera_.yaw=0; camera_.pitch=.28f;
                cleared_=false; moving_=false; quitSelected_=false;
                mouseLocked_=true; window.SetRelativeMouseMode(true);
            }
            return;
        }
        if (input.Pressed(remi::Key::Escape)) {
            moving_=false;
            if (mouseLocked_) { mouseLocked_=false; window.SetRelativeMouseMode(false); }
            else window.RequestClose();
            return;
        }
        if (!mouseLocked_) {
            moving_=false;
            if (input.Pressed(remi::Key::MouseLeft)) {
                mouseLocked_=true; window.SetRelativeMouseMode(true);
            }
            return;
        }
        if (window.Focused()) camera_.Orbit(-input.DeltaX()*mouseScale,input.DeltaY()*mouseScale);
        preferredDistance_=std::clamp(preferredDistance_-input.Wheel()*.5f,1.8f,4.5f);
        const float side=float(input.Held(remi::Key::D))-float(input.Held(remi::Key::A));
        const float forward=float(input.Held(remi::Key::W))-float(input.Held(remi::Key::S));
        const float length=std::sqrt(side*side+forward*forward);
        moving_=length>.01f;
        const float speed=input.Held(remi::Key::Shift)?5.5f:3.4f;
        const float normal=length>1.f?1.f/length:1.f;
        auto velocity=physics_->Velocity(player_);
        velocity.x=(side*std::cos(camera_.yaw)-forward*std::sin(camera_.yaw))*speed*normal;
        velocity.z=(side*std::sin(camera_.yaw)+forward*std::cos(camera_.yaw))*speed*normal;
        physics_->SetVelocity(player_,velocity);
        physics_->Step(static_cast<float>(seconds));
        if (moving_) scene_->Get<remi::TransformComponent>(visual_)->rotation.y=
            std::atan2(velocity.x,velocity.z);
        const auto position=scene_->Get<remi::TransformComponent>(player_)->position;
        if (position.y < -4.f) {
            scene_->Get<remi::TransformComponent>(player_)->position=Spawn();
            physics_->SetVelocity(player_,{});
            moving_=false;
            return;
        }
        const auto exit=Exit();
        const float dx=position.x-exit.x, dz=position.z-exit.z;
        if (dx*dx+dz*dz<1.8f*1.8f) {
            cleared_=true;
            moving_=false; panelVisible_=false; quitSelected_=false;
            physics_->SetVelocity(player_,{});
            mouseLocked_=false;
            window.SetRelativeMouseMode(false);
            remi::Log("Ancient Ruins Maze Escape: CLEAR");
        }
    }
    void OnFrame(remi::Window&,double elapsed,double) override {
        auto& device=render_->Device();
        const auto playerPosition=scene_->Get<remi::TransformComponent>(player_)->position;
        animation_->Update(device,*render_->Meshes().GetMutable(characterMesh_),
            moving_ ? "run" : "idle",elapsed);
        camera_.target={playerPosition.x,playerPosition.y+.45f,playerPosition.z};
        const remi::Vec3 offset{std::sin(camera_.yaw)*std::cos(camera_.pitch),
            std::sin(camera_.pitch),-std::cos(camera_.yaw)*std::cos(camera_.pitch)};
        float safeDistance=.75f;
        for (float distance=.75f; distance<=preferredDistance_; distance+=.12f) {
            const remi::Vec3 candidate{camera_.target.x+offset.x*distance,
                camera_.target.y+offset.y*distance,camera_.target.z+offset.z*distance};
            if (!OpenAt(candidate)) break;
            safeDistance=distance;
        }
        camera_.distance=std::min(preferredDistance_,std::max(.75f,safeDistance-.12f));
        const auto eye=camera_.Position();
        const auto view=camera_.ViewProjection(float(device.Width())/float(device.Height()));
        remi::DirectionalLight light;
        light.direction={-.55f,-1.f,.25f}; light.intensity=.75f;
        light.color={.9f,.94f,1.f}; light.ambient=.36f;
        light.skyAmbient={.29f,.35f,.43f}; light.groundAmbient={.13f,.14f,.16f};
        light.exposure=1.04f; light.fogColor={.59f,.65f,.71f}; light.fogDensity=.011f;
        light.cameraPosition=eye;
        const auto exit=Exit();
        light.pointCount=1;
        light.pointLights[0]={{exit.x,2.3f,exit.z},8.f,{.15f,.7f,1.f},8.f};
        const auto lightView=remi::DirectionalShadowMatrix(eye,light.direction,35.f);
        device.BeginGpuProfile();
        const auto shadow=render_->DrawShadow(*scene_,lightView,{},culling_);
        const auto color=render_->DrawColor(*scene_,view,light,[&](remi::Renderer& renderer) {
            remi::MaterialProperties skyMaterial;
            skyMaterial.shadingModel=remi::ShadingModel::Unlit;
            renderer.DrawLit(*sky_,remi::TransformMatrix(eye,{},{1,1,1}),view,light,skyMaterial);
        },culling_);
        device.ApplyPostProcess();
        if (panelVisible_ && uiFrames_++%12==0) {
            uiStats_.fps=elapsed>0 ? static_cast<unsigned>(std::clamp(1.0/elapsed,0.0,999.0)) : 0u;
            uiStats_.draws=color.draws; uiStats_.shadowDraws=shadow.draws;
            uiStats_.culling=culling_; uiStats_.deferred=device.DeferredEnabled();
        }
        hud_->Update(device,device.Width(),device.Height(),panelVisible_,cleared_,quitSelected_,uiStats_);
        hud_->Draw(device);
        inspector_.Draw(device,*scene_);
        render_->RequireCleanDiagnostics();
        (void)device.EndGpuProfile();
        if (!captureFile.empty() && ++captureFrame_ == 8) device.SaveScreenshot(captureFile);
        device.Present();
        (void)device.PollGpuProfile();
    }
    void OnDetach() noexcept override {
        hud_.reset(); inspector_.Clear();
        physics_.reset(); scene_.reset(); assets_.reset(); jobs_.reset(); sky_.reset(); animation_.reset();
        if (render_) {
            try { (void)render_->ShutdownAndValidate(); }
            catch (const std::exception& e) { remi::Log(e.what()); }
        }
        render_.reset();
    }
private:
    static remi::Vec3 Spawn() { return {World(1),.9f,World(0)}; }
    static remi::Vec3 Exit() { return {World(11),0,World(12)}; }
    bool OpenAt(remi::Vec3 position) const {
        const float origin=World(0)-cell*.5f;
        const int x=static_cast<int>(std::floor((position.x-origin)/cell));
        const int z=static_cast<int>(std::floor((position.z-origin)/cell));
        return x>=0 && x<gridSize && z>=0 && z<gridSize && grid_[z][x];
    }
    void AddCollider(const char* name,remi::Vec3 position,remi::Vec3 half) {
        const auto id=scene_->Create(name);
        scene_->Get<remi::TransformComponent>(id)->position=position;
        remi::BodyDesc body; body.halfExtent=half; physics_->AddBody(id,body);
    }
    std::unique_ptr<remi::SceneRenderSession> render_;
    std::unique_ptr<remi::JobSystem> jobs_;
    std::unique_ptr<remi::assets::StaticGltfCache> assets_;
    std::unique_ptr<remi::Scene> scene_;
    std::unique_ptr<remi::PhysicsWorld> physics_;
    std::unique_ptr<remi::Mesh> sky_;
    std::unique_ptr<remi::BakedAnimation> animation_;
    std::array<std::array<bool,gridSize>,gridSize> grid_{};
    remi::EntityId player_,visual_;
    remi::MeshHandle characterMesh_;
    remi::Camera camera_;
    float preferredDistance_=3.3f;
    std::unique_ptr<MazeHud> hud_;
    MazeHudStats uiStats_{};
    unsigned uiFrames_=0;
    remi::debug::SceneInspector inspector_;
    bool mouseLocked_=false,panelVisible_=false,culling_=true,cleared_=false;
    bool moving_=false,quitSelected_=false;
public:
    fs::path captureFile;
private:
    unsigned captureFrame_=0;
};
}

int wmain(int argc,wchar_t** argv) {
    remi::RunConfig config;
    config.window.title=L"REMI Engine | Ancient Ruins Maze Escape";
    config.window.graphicsSurface=true;
    config.idleWaitMilliseconds=0;
    remi::Application app;
    auto layer=std::make_unique<MazeLayer>();
    for (int i=1; i<argc; ++i) {
        if (std::wstring_view(argv[i])==L"--capture" && i+1<argc)
            layer->captureFile=argv[++i];
        else return 2;
    }
    if (!layer->captureFile.empty()) {
        config.window.visible=false;
        config.pauseWhenInactive=false;
        config.frameLimit=12;
    }
    app.PushLayer(std::move(layer));
    return remi::RunApplication(app,config);
}
