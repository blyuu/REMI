#include <remi/assets/GltfAsset.hpp>
#include <remi/assets/TextureLoader.hpp>
#include <remi/assets/MaterialAsset.hpp>
#include <DirectXMath.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace remi::assets {
namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
Vertex Convert(const gltf::Vertex& source) {
    // glTF is right handed; REMI's camera/projection is left handed.
    return {{source.position.x, source.position.y, -source.position.z},
        {source.color.x, source.color.y, source.color.z},
        {source.normal.x, source.normal.y, -source.normal.z},
        {source.uv0.x, source.uv0.y}};
}
MaterialProperties Convert(const gltf::Material& source) {
    MaterialProperties value;
    value.tint = {source.baseColorFactor.x, source.baseColorFactor.y, source.baseColorFactor.z};
    value.metallic = std::clamp(source.metallic, 0.f, 1.f);
    value.roughness = std::clamp(source.roughness, .04f, 1.f);
    return value;
}
}
std::unique_ptr<GltfAsset> GltfAsset::Load(Renderer& renderer, const std::filesystem::path& file) {
    auto asset = std::unique_ptr<GltfAsset>(new GltfAsset());
    if (!gltf::Load(Utf8(file), asset->model_)) throw std::runtime_error("Could not import glTF: " + Utf8(file));
    asset->animator_.SetModel(&asset->model_);
    asset->primitives_.reserve(asset->model_.primitives.size());
    for (std::size_t i = 0; i < asset->model_.primitives.size(); ++i) {
        const auto& source = asset->model_.primitives[i];
        if (source.vertices.empty() || source.indices.empty()) continue;
        Primitive primitive;
        primitive.sourceIndex = i;
        primitive.scratch.reserve(source.vertices.size());
        for (const auto& vertex : source.vertices) primitive.scratch.push_back(Convert(vertex));
        auto indices = source.indices;
        for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
            std::swap(indices[triangle + 1], indices[triangle + 2]);
        primitive.mesh = source.hasSkin
            ? renderer.CreateDynamicMesh(primitive.scratch, indices)
            : renderer.CreateMesh(primitive.scratch, indices);
        if (source.material >= 0) {
            const auto& material = asset->model_.materials[static_cast<std::size_t>(source.material)];
            primitive.material = Convert(material);
            const auto overrideFile = file.parent_path() / "materials" /
                (file.stem().wstring() + L"_" + std::to_wstring(source.material) + L".remimat");
            std::filesystem::path overrideTexture;
            if (std::filesystem::exists(overrideFile)) {
                auto override = LoadMaterial(overrideFile);
                primitive.material = override.properties;
                overrideTexture = std::move(override.baseColorTexture);
            }
            if (!overrideTexture.empty()) {
                const auto decoded = LoadImage(overrideTexture);
                renderer.SetMeshTexture(*primitive.mesh, decoded.width, decoded.height, decoded.pixels);
            } else if (const int imageIndex = material.baseColorImage; imageIndex >= 0) {
                const auto& image = asset->model_.images[static_cast<std::size_t>(imageIndex)];
                ImageRGBA decoded;
                if (!image.data.empty()) decoded = DecodeImage(image.data);
                else if (!image.uri.empty()) {
                    const auto relative = std::filesystem::path(std::u8string(image.uri.begin(), image.uri.end()));
                    decoded = LoadImage(file.parent_path() / relative);
                }
                if (!decoded.pixels.empty())
                    renderer.SetMeshTexture(*primitive.mesh, decoded.width, decoded.height, decoded.pixels);
            }
        }
        asset->primitives_.push_back(std::move(primitive));
    }
    if (asset->primitives_.empty()) throw std::runtime_error("glTF has no drawable triangle primitives");
    return asset;
}
std::vector<std::string> GltfAsset::ClipNames() const {
    std::vector<std::string> names;
    names.reserve(model_.animations.size());
    for (const auto& clip : model_.animations) names.push_back(clip.name);
    return names;
}
bool GltfAsset::SetClip(std::string_view name, float crossfadeSeconds) {
    for (std::size_t i = 0; i < model_.animations.size(); ++i) {
        if (model_.animations[i].name == name) {
            if (crossfadeSeconds <= 0) animator_.SetClip(static_cast<int>(i));
            else animator_.CrossfadeTo(static_cast<int>(i), crossfadeSeconds);
            return true;
        }
    }
    return false;
}
void GltfAsset::Update(Renderer& renderer, float dt) {
    if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("Invalid animation timestep");
    if (!HasSkeleton()) return;
    if (states_.Current() >= 0) states_.Update(animator_, dt);
    else animator_.Update(dt);
    std::vector<DirectX::XMFLOAT4X4> palette;
    animator_.ComputeBoneMatrices(palette);
    std::vector<DirectX::XMMATRIX> normalPalette;
    normalPalette.reserve(palette.size());
    for (const auto& bone : palette) {
        auto matrix = DirectX::XMLoadFloat4x4(&bone);
        DirectX::XMVECTOR determinant;
        normalPalette.push_back(DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&determinant, matrix)));
    }
    for (auto& primitive : primitives_) {
        const auto& source = model_.primitives[primitive.sourceIndex];
        if (!source.hasSkin) continue;
        for (std::size_t i = 0; i < source.vertices.size(); ++i) {
            const auto& input = source.vertices[i];
            using namespace DirectX;
            const auto position = XMLoadFloat3(&input.position);
            const auto normal = XMLoadFloat3(&input.normal);
            XMVECTOR blendedPosition = XMVectorZero(), blendedNormal = XMVectorZero();
            const float weights[] = {input.weights.x, input.weights.y, input.weights.z, input.weights.w};
            for (unsigned joint = 0; joint < 4; ++joint) {
                if (weights[joint] <= 0) continue;
                const auto matrix = XMLoadFloat4x4(&palette[input.joints[joint]]);
                blendedPosition = XMVectorAdd(blendedPosition, XMVectorScale(XMVector3TransformCoord(position, matrix), weights[joint]));
                blendedNormal = XMVectorAdd(blendedNormal,
                    XMVectorScale(XMVector3TransformNormal(normal, normalPalette[input.joints[joint]]), weights[joint]));
            }
            XMFLOAT3 p{}, n{};
            XMStoreFloat3(&p, blendedPosition);
            XMStoreFloat3(&n, XMVector3Normalize(blendedNormal));
            primitive.scratch[i].position = {p.x, p.y, -p.z};
            primitive.scratch[i].normal = {n.x, n.y, -n.z};
        }
        renderer.UpdateMeshVertices(*primitive.mesh, primitive.scratch);
    }
}
void GltfAsset::Draw(Renderer& renderer, const Matrix4& world, const Matrix4& viewProjection, const DirectionalLight& light) const {
    for (const auto& primitive : primitives_) {
        auto material = primitive.material;
        material.tint = {material.tint.x * globalTint_.x, material.tint.y * globalTint_.y, material.tint.z * globalTint_.z};
        renderer.DrawLit(*primitive.mesh, world, viewProjection, light, material);
    }
}
void GltfAsset::DrawShadow(Renderer& renderer, const Matrix4& world, const Matrix4& lightViewProjection) const {
    const auto mvp = Multiply(world, lightViewProjection);
    for (const auto& primitive : primitives_) renderer.Draw(*primitive.mesh, mvp);
}
std::vector<MeshHandle> ImportStaticGltfScene(Renderer& renderer, Scene& scene,
    ResourceCache<Mesh>& meshes, const std::filesystem::path& file) {
    auto asset = GltfAsset::Load(renderer, file);
    if (asset->HasSkeleton()) throw std::invalid_argument("Use GltfAsset for a skinned scene");
    static std::atomic_uint64_t importId{1};
    const auto id = importId.fetch_add(1);
    std::vector<MeshHandle> handles;
    std::vector<EntityId> entities;
    try {
        for (std::size_t i = 0; i < asset->primitives_.size(); ++i) {
            auto& primitive = asset->primitives_[i];
            const auto handle = meshes.Load("gltf:" + std::to_string(id) + ":" + std::to_string(i),
                [&] { return std::move(primitive.mesh); });
            handles.push_back(handle);
            const auto entity = scene.Create(Utf8(file.filename()) + "/primitive_" + std::to_string(i));
            entities.push_back(entity);
            scene.Add<MeshComponent>(entity, {handle, true, primitive.material});
        }
    } catch (...) {
        for (auto entity : entities) (void)scene.Destroy(entity);
        for (auto handle : handles) (void)meshes.Unload(handle);
        throw;
    }
    return handles;
}
}
