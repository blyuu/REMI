#include "GravityLevelBuilder.hpp"
#include <algorithm>
#include <utility>

namespace remi::game {
BuiltGravityLevel BuildGravityLevel(Scene& scene, PhysicsWorld& physics,
    const GravityLevel& level, const GravityMeshes& meshes, bool separatePlayerVisual) {
    SceneBuilder builder(scene,physics);
    const auto box = [&](std::string name, Vec3 position, Vec3 half, MeshHandle mesh,
        bool collision = false, BodyType type = BodyType::Static) {
        BoxSpawnDesc desc; desc.name = std::move(name); desc.position = position;
        desc.halfExtent = half; desc.mesh = mesh;
        if (collision) { BodyDesc body; body.type = type; body.halfExtent = half; desc.body = body; }
        return builder.SpawnBox(desc);
    };
    (void)box("Start platform",level.platforms[0].position,level.platforms[0].halfExtent,meshes.platform,true);
    (void)box("Finish platform",level.platforms[1].position,level.platforms[1].halfExtent,meshes.platform,true);
    (void)box("Ceiling bridge",level.platforms[2].position,level.platforms[2].halfExtent,meshes.platform,true);
    (void)box("Green goal",level.goalPosition,level.goalHalfExtent,meshes.goal);
    BuiltGravityLevel built;
    for (std::size_t i=0;i<built.coins.size();++i) {
        const auto position = level.coinPositions[i];
        built.coins[i] = box("Gravity coin",position,{.35f,.35f,.12f},meshes.coin);
        if (auto* mesh = scene.Get<MeshComponent>(built.coins[i])) mesh->material = {{1.f,.9f,.35f},.7f,.25f,.55f};
        (void)box("Coin marker",{position.x,position.y+.615f,position.z},
            {.65f,.015f,.65f},meshes.accent);
    }
    const auto bridge = level.platforms[2];
    const float guideY = bridge.position.y-bridge.halfExtent.y-.035f;
    const float guideZ = std::max(bridge.halfExtent.z-.18f,0.f);
    const float guideX = std::max(bridge.halfExtent.x-.2f,.1f);
    (void)box("Ceiling guide left",{bridge.position.x,guideY,bridge.position.z-guideZ},
        {guideX,.025f,.035f},meshes.accent);
    (void)box("Ceiling guide right",{bridge.position.x,guideY,bridge.position.z+guideZ},
        {guideX,.025f,.035f},meshes.accent);
    const auto edge = [&](const char* name,const LevelBox& platform,float x) {
        (void)box(name,{x,platform.position.y+platform.halfExtent.y+.025f,platform.position.z},
            {.08f,.025f,std::max(platform.halfExtent.z-.2f,.1f)},meshes.accent);
    };
    edge("Start edge",level.platforms[0],level.platforms[0].position.x+level.platforms[0].halfExtent.x);
    edge("Finish edge",level.platforms[1],level.platforms[1].position.x-level.platforms[1].halfExtent.x);
    const auto goal = level.goalPosition;
    const float archBase = level.platforms[1].position.y+level.platforms[1].halfExtent.y;
    const float archOffset = level.goalHalfExtent.z+.35f;
    (void)box("Finish arch left",{goal.x,archBase+.8f,goal.z-archOffset},{.06f,.8f,.06f},meshes.accent);
    (void)box("Finish arch right",{goal.x,archBase+.8f,goal.z+archOffset},{.06f,.8f,.06f},meshes.accent);
    (void)box("Finish arch beam",{goal.x,archBase+1.6f,goal.z},{.06f,.06f,archOffset+.06f},meshes.accent);
    built.player = box("Player",level.playerSpawn,{.35f,.4f,.35f},
        separatePlayerVisual ? MeshHandle{} : meshes.player,true,BodyType::Dynamic);
    if (separatePlayerVisual)
        built.playerVisual = builder.SpawnVisualChild("Player visual",built.player,meshes.player);
    return built;
}

GravityLevel MakeAlternateGravityLevel(GravityLevel base) {
    // A second code-authored course using the same Scene, physics, and game rules.
    base.platforms[0].halfExtent.x = 2.5f;
    base.platforms[1].halfExtent.x = 2.5f;
    base.platforms[2].position.y += 1.f;
    for (auto& coin : base.coinPositions) coin.y += 1.f;
    base.deathMax.y += 1.f;
    return base;
}
}
