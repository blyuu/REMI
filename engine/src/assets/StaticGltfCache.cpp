#include <remi/assets/StaticGltfCache.hpp>
#include <atomic>
#include <stdexcept>

namespace remi::assets {
StaticGltfCache::StaticGltfCache(Renderer& renderer, ResourceCache<Mesh>& meshes)
    : renderer_(renderer), meshes_(meshes) {
    static std::atomic_uint64_t next{1};
    prefix_ = "static-gltf:" + std::to_string(next.fetch_add(1)) + ":";
}
StaticGltfCache::~StaticGltfCache() { Clear(); }
std::string StaticGltfCache::Key(const std::filesystem::path& file) {
    const auto path = std::filesystem::weakly_canonical(file).generic_u8string();
    return {path.begin(),path.end()};
}
StaticGltfCache::Record& StaticGltfCache::Load(const std::filesystem::path& file) {
    const auto key = Key(file);
    if (const auto found = records_.find(key); found != records_.end()) return found->second;
    auto asset = GltfAsset::Load(renderer_,file);
    if (asset->HasSkeleton()) throw std::invalid_argument("Static cache does not support animated assets");
    auto [entry,inserted] = records_.try_emplace(key);
    (void)inserted;
    auto& record = entry->second;
    try {
        record.handles.reserve(asset->primitives_.size());
        record.keys.reserve(asset->primitives_.size());
        record.materials.reserve(asset->primitives_.size());
        for (std::size_t i = 0; i < asset->primitives_.size(); ++i) {
            auto& primitive = asset->primitives_[i];
            record.keys.push_back(prefix_ + key + ":" + std::to_string(i));
            const auto handle = meshes_.Load(record.keys.back(),[&] { return std::move(primitive.mesh); });
            record.handles.push_back(handle);
            record.materials.push_back(primitive.material);
        }
    } catch (...) {
        for (auto handle : record.handles) (void)meshes_.Unload(handle);
        records_.erase(entry);
        throw;
    }
    return record;
}
std::vector<EntityId> StaticGltfCache::Instantiate(Scene& scene, const std::filesystem::path& file) {
    const auto& record = Load(file);
    for (auto handle : record.handles)
        if (!meshes_.Get(handle)) throw std::logic_error("Static asset mesh was evicted outside its owner");
    std::vector<EntityId> entities;
    entities.reserve(record.handles.size());
    try {
        for (std::size_t i = 0; i < record.handles.size(); ++i) {
            const auto entity = scene.Create("glTF primitive " + std::to_string(i));
            entities.push_back(entity);
            scene.Add<MeshComponent>(entity,{record.handles[i],true,record.materials[i]});
        }
    } catch (...) {
        for (auto entity : entities) (void)scene.Destroy(entity);
        throw;
    }
    return entities;
}
void StaticGltfCache::Reload(const std::filesystem::path& file) {
    const auto found = records_.find(Key(file));
    if (found == records_.end()) { (void)Load(file); return; }
    auto replacement = GltfAsset::Load(renderer_,file);
    auto& record = found->second;
    if (replacement->HasSkeleton() || replacement->primitives_.size() != record.handles.size())
        throw std::invalid_argument("Reload requires the same static primitive count; unload/reinstantiate for topology changes");
    for (auto handle : record.handles)
        if (!meshes_.Get(handle)) throw std::logic_error("Static asset mesh was evicted outside its owner");
    // Everything that can allocate or decode completed above. Existing cache keys
    // replace unique_ptrs without allocating, and all observers keep their handles.
    for (std::size_t i = 0; i < record.handles.size(); ++i) {
        (void)meshes_.Reload(record.keys[i],[&] { return std::move(replacement->primitives_[i].mesh); });
        record.materials[i] = replacement->primitives_[i].material;
    }
}
bool StaticGltfCache::Unload(const std::filesystem::path& file) {
    const auto found = records_.find(Key(file));
    if (found == records_.end()) return false;
    for (auto handle : found->second.handles) (void)meshes_.Unload(handle);
    records_.erase(found);
    return true;
}
void StaticGltfCache::Clear() {
    for (const auto& [key,record] : records_) {
        (void)key;
        for (auto handle : record.handles) (void)meshes_.Unload(handle);
    }
    records_.clear();
}
}
