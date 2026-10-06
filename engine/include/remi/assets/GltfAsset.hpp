#pragma once
#include <remi/animation/AnimStateMachine.hpp>
#include <remi/animation/Animator.hpp>
#include <remi/core/JobSystem.hpp>
#include <remi/render/Renderer.hpp>
#include <remi/resources/ResourceCache.hpp>
#include <remi/scene/Scene.hpp>
#include <filesystem>
#include <memory>

namespace remi::assets {
// Runtime GLB/glTF asset with per-primitive materials and optional CPU skeletal skinning.
// Owns its GPU meshes; destroy this before the Renderer.
class GltfAsset {
public:
    [[nodiscard]] static std::unique_ptr<GltfAsset> Load(Renderer& renderer, const std::filesystem::path& file,
                                                         JobSystem* jobs = nullptr);
    [[nodiscard]] bool HasSkeleton() const noexcept { return model_.HasSkeleton(); }
    [[nodiscard]] std::size_t PrimitiveCount() const noexcept { return primitives_.size(); }
    [[nodiscard]] const MaterialProperties& MaterialAt(std::size_t primitive) const { return primitives_.at(primitive).material; }
    [[nodiscard]] std::vector<std::string> ClipNames() const;
    [[nodiscard]] bool SetClip(std::string_view name, float crossfadeSeconds = .2f);
    void Update(Renderer& renderer, float dt);
    void Draw(Renderer& renderer, const Matrix4& world, const Matrix4& viewProjection, const DirectionalLight& light) const;
    void DrawShadow(Renderer& renderer, const Matrix4& world, const Matrix4& lightViewProjection) const;
    [[nodiscard]] Animator& Animation() noexcept { return animator_; }
    [[nodiscard]] AnimStateMachine& States() noexcept { return states_; }
    void SetGlobalTint(Vec3 tint) noexcept { globalTint_ = tint; }
private:
    friend class StaticGltfCache;
    friend std::vector<MeshHandle> ImportStaticGltfScene(Renderer&, Scene&, ResourceCache<Mesh>&, const std::filesystem::path&);
    struct Primitive {
        std::unique_ptr<Mesh> mesh;
        std::vector<Vertex> scratch;
        MaterialProperties material;
        std::size_t sourceIndex = 0;
    };
    gltf::Model model_;
    Animator animator_;
    AnimStateMachine states_;
    std::vector<Primitive> primitives_;
    Vec3 globalTint_{1,1,1};
};

// Imports a static glTF scene into the ordinary Scene/mesh-cache path.
// Mesh ownership stays with the supplied cache; returned handles can be unloaded by the caller.
[[nodiscard]] std::vector<MeshHandle> ImportStaticGltfScene(Renderer& renderer, Scene& scene,
    ResourceCache<Mesh>& meshes, const std::filesystem::path& file);
}
