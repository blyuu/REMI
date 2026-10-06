#pragma once
#include <remi/assets/GltfAsset.hpp>
#include <map>

namespace remi::assets {
// Main-thread static-model cache. Renderer and mesh cache must outlive this object.
// Scene instances borrow handles. Clearing a Scene does not evict a shared model;
// Unload/Clear (or this object's destructor) explicitly invalidates those handles.
class StaticGltfCache {
public:
    StaticGltfCache(Renderer& renderer, ResourceCache<Mesh>& meshes, JobSystem* jobs = nullptr);
    ~StaticGltfCache();
    StaticGltfCache(const StaticGltfCache&) = delete;
    StaticGltfCache& operator=(const StaticGltfCache&) = delete;
    [[nodiscard]] std::vector<EntityId> Instantiate(Scene& scene, const std::filesystem::path& file);
    // Geometry/textures replace atomically after complete loading, preserving handles.
    // Primitive count changes are rejected. Existing per-entity material overrides stay.
    void Reload(const std::filesystem::path& file);
    bool Unload(const std::filesystem::path& file);
    void Clear();
    [[nodiscard]] std::size_t Size() const noexcept { return records_.size(); }
private:
    struct Record {
        std::vector<MeshHandle> handles;
        std::vector<std::string> keys;
        std::vector<MaterialProperties> materials;
    };
    Record& Load(const std::filesystem::path& file);
    [[nodiscard]] static std::string Key(const std::filesystem::path& file);
    Renderer& renderer_;
    ResourceCache<Mesh>& meshes_;
    JobSystem* jobs_;
    std::string prefix_;
    std::map<std::string,Record> records_;
};
}
