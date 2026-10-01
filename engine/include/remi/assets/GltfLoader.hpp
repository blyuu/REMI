// asset/GltfLoader.h — loads a glTF/GLB into per-primitive CPU meshes.
#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

namespace gltf {

// Interleaved vertex covering everything the engine's shaders may consume.
// Missing attributes get sensible defaults so every primitive is uniform.
struct Vertex
{
    DirectX::XMFLOAT3 position { 0, 0, 0 };
    DirectX::XMFLOAT3 normal   { 0, 0, 1 };
    DirectX::XMFLOAT4 tangent  { 1, 0, 0, 1 };
    DirectX::XMFLOAT2 uv0      { 0, 0 };
    DirectX::XMFLOAT4 color    { 1, 1, 1, 1 }; // COLOR_0 (many textureless models color here)
    uint32_t          joints[4] { 0, 0, 0, 0 };
    DirectX::XMFLOAT4 weights  { 0, 0, 0, 0 };
};

// One glTF primitive = one submesh with a single material.
struct Primitive
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    int  material  = -1;    // index into Model::... (materials wired in S3.2)
    bool hasSkin   = false; // JOINTS_0 + WEIGHTS_0 present
};

// A glTF material: PBR factors + indices into Model::images (-1 = none).
struct Material
{
    DirectX::XMFLOAT4 baseColorFactor { 1, 1, 1, 1 };
    float metallic  = 1.0f;
    float roughness = 1.0f;
    int   baseColorImage         = -1;
    int   normalImage            = -1;
    int occlusionImage = -1;
    float occlusionStrength = 1;
    int   metallicRoughnessImage = -1;
    bool  alphaMask   = false;
    float alphaCutoff = 0.5f;
    bool  doubleSided = false;
    std::string name;
};

// Encoded (still-compressed) image: either GLB-embedded bytes or an external uri.
struct EncodedImage
{
    std::vector<uint8_t> data; // PNG/JPG bytes (embedded); empty if external
    std::string          uri;  // relative path (external); empty if embedded
};

// ---- skeletal animation data ----
struct NodeData
{
    std::string       name;
    DirectX::XMFLOAT3 translation { 0, 0, 0 };
    DirectX::XMFLOAT4 rotation    { 0, 0, 0, 1 };
    DirectX::XMFLOAT3 scale       { 1, 1, 1 };
    int parent = -1;
};

struct SkinData
{
    std::vector<int>                 joints;      // node index per joint
    std::vector<DirectX::XMFLOAT4X4> inverseBind; // row-vector convention
};

struct AnimSampler
{
    std::vector<float> times;
    std::vector<float> values; // flattened, `comps` per key
    std::vector<float> inTangents, outTangents;
    int  comps = 4;            // 3 = vec3 (T/S), 4 = quat (R)
    bool step  = false;        // STEP vs LINEAR interpolation
    bool cubic = false;
};

struct AnimChannel
{
    int node    = -1;
    int path    = 0; // 0 = translation, 1 = rotation, 2 = scale
    int sampler = -1;
};

struct Animation
{
    std::string name;
    float duration = 0.0f;
    std::vector<AnimSampler> samplers;
    std::vector<AnimChannel> channels;
};

struct Model
{
    std::vector<Primitive>    primitives;
    std::vector<Material>     materials;
    std::vector<EncodedImage> images;

    std::vector<NodeData>  nodes;      // full hierarchy (for skinning)
    SkinData               skin;       // first skin, if any
    std::vector<Animation> animations;

    bool HasSkeleton() const { return !skin.joints.empty(); }

    size_t TotalVertices() const;
    size_t TotalIndices()  const;
};

// Loads a .glb/.gltf. Returns false and logs on failure.
bool Load(const std::string& path, Model& out);

} // namespace gltf
