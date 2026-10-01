#include <remi/assets/GltfLoader.hpp>
#include <remi/core/Log.hpp>

#include "cgltf.h"

#include <cstring>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace {
template<class... Args> void ImportLog(const char* format, Args... args) {
    char text[1024]{};
    std::snprintf(text, sizeof(text), format, args...);
    remi::Log(text);
}
}
#define LOG_ERROR(...) ImportLog(__VA_ARGS__)
#define LOG_WARN(...) ImportLog(__VA_ARGS__)

using namespace DirectX;

namespace gltf {

size_t Model::TotalVertices() const
{
    size_t n = 0;
    for (const auto& p : primitives) n += p.vertices.size();
    return n;
}

size_t Model::TotalIndices() const
{
    size_t n = 0;
    for (const auto& p : primitives) n += p.indices.size();
    return n;
}

namespace {

std::vector<uint8_t> DecodeDataUri(std::string_view uri) {
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos)
        return {};
    uri.remove_prefix(comma + 1);
    if (uri.size() > 128ull * 1024 * 1024) return {};
    std::vector<uint8_t> bytes;
    bytes.reserve(uri.size() * 3 / 4);
    unsigned bits = 0, count = 0;
    for (char c : uri) {
        if (c == '=') break;
        unsigned digit;
        if (c >= 'A' && c <= 'Z') digit = static_cast<unsigned>(c - 'A');
        else if (c >= 'a' && c <= 'z') digit = static_cast<unsigned>(c - 'a' + 26);
        else if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0' + 52);
        else if (c == '+') digit = 62;
        else if (c == '/') digit = 63;
        else return {};
        bits = (bits << 6) | digit;
        count += 6;
        if (count >= 8) {
            count -= 8;
            bytes.push_back(static_cast<uint8_t>((bits >> count) & 255));
        }
    }
    return bytes;
}

// cgltf's default fopen uses the Windows ANSI code page. Import paths are UTF-8.
cgltf_result ReadFile(const cgltf_memory_options*, const cgltf_file_options*,
                     const char* path, cgltf_size* size, void** data)
{
    std::ifstream file(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))), std::ios::binary | std::ios::ate);
    if (!file) return cgltf_result_file_not_found;
    const auto length = file.tellg();
    if (length < 0 || (*size && static_cast<size_t>(length) < *size)) return cgltf_result_io_error;
    const size_t bytes = *size ? *size : static_cast<size_t>(length);
    void* buffer = std::malloc(bytes ? bytes : 1);
    if (!buffer) return cgltf_result_out_of_memory;
    file.seekg(0);
    if (!file.read(static_cast<char*>(buffer), bytes))
    {
        std::free(buffer);
        return cgltf_result_io_error;
    }
    *data = buffer;
    *size = bytes;
    return cgltf_result_success;
}

void ReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data, cgltf_size)
{
    std::free(data);
}

// Find a primitive attribute by semantic type (index 0 for _0 slots).
const cgltf_accessor* FindAttribute(const cgltf_primitive& prim, cgltf_attribute_type type)
{
    for (cgltf_size a = 0; a < prim.attributes_count; ++a)
        if (prim.attributes[a].type == type && prim.attributes[a].index == 0)
            return prim.attributes[a].data;
    return nullptr;
}

// Resolve a texture view's underlying image to an index into data->images.
int ImageIndex(const cgltf_data* data, const cgltf_texture_view& view)
{
    if (!view.texture || !view.texture->image) return -1;
    return static_cast<int>(view.texture->image - data->images);
}

// Process one primitive into a submesh, baking `world` into positions/normals.
void ProcessPrimitive(const cgltf_data* data, const cgltf_primitive& prim,
                      FXMMATRIX world, CXMMATRIX normalMat, bool skinned, Model& out)
{
    if (prim.type != cgltf_primitive_type_triangles) return;

    const cgltf_accessor* aPos     = FindAttribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* aNrm     = FindAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* aTan     = FindAttribute(prim, cgltf_attribute_type_tangent);
    const cgltf_accessor* aUv      = FindAttribute(prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor* aColor   = FindAttribute(prim, cgltf_attribute_type_color);
    const cgltf_accessor* aJoints  = FindAttribute(prim, cgltf_attribute_type_joints);
    const cgltf_accessor* aWeights = FindAttribute(prim, cgltf_attribute_type_weights);
    if (!aPos) { LOG_WARN("primitive without POSITION skipped"); return; }

    Primitive sub;
    sub.hasSkin  = skinned && (aJoints != nullptr && aWeights != nullptr);
    sub.material = prim.material ? static_cast<int>(prim.material - data->materials) : -1;

    const cgltf_size vcount = aPos->count;
    sub.vertices.resize(vcount);
    for (cgltf_size i = 0; i < vcount; ++i)
    {
        Vertex& v = sub.vertices[i];
        cgltf_accessor_read_float(aPos, i, &v.position.x, 3);
        if (aNrm) cgltf_accessor_read_float(aNrm, i, &v.normal.x, 3);
        if (aTan) cgltf_accessor_read_float(aTan, i, &v.tangent.x, 4);
        if (aUv)  cgltf_accessor_read_float(aUv, i, &v.uv0.x, 2);
        if (aColor)
        {
            cgltf_accessor_read_float(aColor, i, &v.color.x, 4);
            if (aColor->type == cgltf_type_vec3) v.color.w = 1.0f; // vec3 colors have no alpha
        }
        if (aWeights) cgltf_accessor_read_float(aWeights, i, &v.weights.x, 4);
        if (aJoints)
        {
            cgltf_uint j[4] = { 0, 0, 0, 0 };
            cgltf_accessor_read_uint(aJoints, i, j, 4);
            for (int k = 0; k < 4; ++k) v.joints[k] = j[k];
        }

        // Bake the node transform for static meshes; skinned meshes stay in skin space.
        if (!skinned)
        {
            XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&v.position), world);
            XMStoreFloat3(&v.position, p);
            XMVECTOR n = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&v.normal), normalMat));
            XMStoreFloat3(&v.normal, n);
            XMVECTOR tg = XMLoadFloat4(&v.tangent);
            XMVECTOR t3 = XMVector3Normalize(XMVector3TransformNormal(tg, normalMat));
            XMStoreFloat4(&v.tangent, XMVectorSetW(t3, XMVectorGetW(tg)));
        }
    }

    if (prim.indices)
    {
        const cgltf_size icount = prim.indices->count;
        sub.indices.resize(icount);
        for (cgltf_size i = 0; i < icount; ++i)
            sub.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
    }
    else
    {
        sub.indices.resize(vcount);
        for (cgltf_size i = 0; i < vcount; ++i) sub.indices[i] = static_cast<uint32_t>(i);
    }

    out.primitives.push_back(std::move(sub));
}

// Parse all animations from `data` and append to `out`. Channel node indices are
// relative to data->nodes; callers merging across files must ensure the node
// hierarchy matches (same export/rig).
void AppendAnimations(const cgltf_data* data, std::vector<Animation>& out)
{
    for (cgltf_size a = 0; a < data->animations_count; ++a)
    {
        const cgltf_animation& anim = data->animations[a];
        Animation ad;
        if (anim.name) ad.name = anim.name;

        ad.samplers.resize(anim.samplers_count);
        for (cgltf_size s = 0; s < anim.samplers_count; ++s)
        {
            const cgltf_animation_sampler& src = anim.samplers[s];
            AnimSampler& as = ad.samplers[s];
            const cgltf_size keys = src.input->count;
            const bool cubic = (src.interpolation == cgltf_interpolation_type_cubic_spline);
            as.cubic = cubic;
            as.step  = (src.interpolation == cgltf_interpolation_type_step);
            as.comps = (src.output->type == cgltf_type_vec4) ? 4 : 3;

            as.times.resize(keys);
            for (cgltf_size k = 0; k < keys; ++k)
                cgltf_accessor_read_float(src.input, k, &as.times[k], 1);

            as.values.resize(keys * as.comps);
            if (cubic)
            {
                as.inTangents.resize(keys * as.comps);
                as.outTangents.resize(keys * as.comps);
            }
            for (cgltf_size k = 0; k < keys; ++k)
            {
                const cgltf_size srcIdx = cubic ? (k * 3 + 1) : k; // cubic: take the value component
                cgltf_accessor_read_float(src.output, srcIdx, &as.values[k * as.comps], as.comps);
                if (cubic)
                {
                    cgltf_accessor_read_float(src.output, k * 3, &as.inTangents[k * as.comps], as.comps);
                    cgltf_accessor_read_float(src.output, k * 3 + 2, &as.outTangents[k * as.comps], as.comps);
                }
            }
            if (!as.times.empty()) ad.duration = (std::max)(ad.duration, as.times.back());
        }

        ad.channels.resize(anim.channels_count);
        for (cgltf_size c = 0; c < anim.channels_count; ++c)
        {
            const cgltf_animation_channel& src = anim.channels[c];
            AnimChannel& ch = ad.channels[c];
            ch.node    = src.target_node ? static_cast<int>(src.target_node - data->nodes) : -1;
            ch.sampler = static_cast<int>(src.sampler - anim.samplers);
            switch (src.target_path)
            {
            case cgltf_animation_path_type_translation: ch.path = 0; break;
            case cgltf_animation_path_type_rotation:    ch.path = 1; break;
            case cgltf_animation_path_type_scale:       ch.path = 2; break;
            default:                                    ch.path = -1; break; // weights etc, skip
            }
        }
        out.push_back(std::move(ad));
    }
}

} // namespace

bool Load(const std::string& path, Model& out)
{
    out = Model{};

    cgltf_options options{};
    options.file.read = ReadFile;
    options.file.release = ReleaseFile;
    cgltf_data* data = nullptr;

    cgltf_result r = cgltf_parse_file(&options, path.c_str(), &data);
    if (r != cgltf_result_success)
    {
        LOG_ERROR("cgltf_parse_file failed (%d): %s", r, path.c_str());
        return false;
    }

    r = cgltf_load_buffers(&options, data, path.c_str());
    if (r != cgltf_result_success)
    {
        LOG_ERROR("cgltf_load_buffers failed (%d): %s", r, path.c_str());
        cgltf_free(data);
        return false;
    }

    if (cgltf_validate(data) != cgltf_result_success || data->skins_count > 1 ||
        (data->skins_count && data->skins[0].joints_count > 512))
    {
        LOG_ERROR("Invalid glTF or unsupported skin count/palette size: %s", path.c_str());
        cgltf_free(data);
        return false;
    }

    // ---- images (extract still-encoded bytes; decode happens in TextureLoader) ----
    out.images.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i)
    {
        const cgltf_image& img = data->images[i];
        EncodedImage& dst = out.images[i];
        if (img.buffer_view) // GLB-embedded
        {
            const cgltf_buffer_view* bv = img.buffer_view;
            const uint8_t* src = static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
            dst.data.assign(src, src + bv->size);
        }
        else if (img.uri && strncmp(img.uri, "data:", 5) == 0)
        {
            dst.data = DecodeDataUri(img.uri);
            if (dst.data.empty()) LOG_WARN("image %zu has an unsupported data URI", i);
        }
        else if (img.uri)
        {
            dst.uri = img.uri; // external file (resolved against the glb dir by the caller)
            dst.uri.resize(cgltf_decode_uri(dst.uri.data()));
        }
        else
        {
            LOG_WARN("image %zu uses an unsupported source (data: URI?)", i);
        }
    }

    // ---- materials ----
    out.materials.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material& src = data->materials[i];
        Material& dst = out.materials[i];
        if (src.name) dst.name = src.name;

        if (src.has_pbr_metallic_roughness)
        {
            const auto& pbr = src.pbr_metallic_roughness;
            dst.baseColorFactor = { pbr.base_color_factor[0], pbr.base_color_factor[1],
                                    pbr.base_color_factor[2], pbr.base_color_factor[3] };
            dst.metallic  = pbr.metallic_factor;
            dst.roughness = pbr.roughness_factor;
            dst.baseColorImage         = ImageIndex(data, pbr.base_color_texture);
            dst.metallicRoughnessImage = ImageIndex(data, pbr.metallic_roughness_texture);
        }
        dst.occlusionImage=ImageIndex(data,src.occlusion_texture);
        dst.occlusionStrength=src.occlusion_texture.scale;
        dst.normalImage  = ImageIndex(data, src.normal_texture);
        dst.doubleSided  = src.double_sided != 0;
        dst.alphaMask    = (src.alpha_mode == cgltf_alpha_mode_mask);
        dst.alphaCutoff  = src.alpha_cutoff;
    }

    // ---- node hierarchy (local TRS + parent) for skinning ----
    out.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node& node = data->nodes[i];
        NodeData& nd = out.nodes[i];
        if (node.name) nd.name = node.name;
        if (node.has_matrix)
        {
            XMFLOAT4X4 tmp; memcpy(&tmp, node.matrix, sizeof(node.matrix));
            // Column-major glTF bytes already represent the transpose when read
            // as a DirectX row-major, row-vector matrix. Do not transpose again.
            XMMATRIX M = XMLoadFloat4x4(&tmp);
            XMVECTOR s, rotation, t;
            XMMatrixDecompose(&s, &rotation, &t, M);
            XMStoreFloat3(&nd.scale, s);
            XMStoreFloat4(&nd.rotation, rotation);
            XMStoreFloat3(&nd.translation, t);
        }
        else
        {
            if (node.has_translation) nd.translation = { node.translation[0], node.translation[1], node.translation[2] };
            if (node.has_rotation)    nd.rotation    = { node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] };
            if (node.has_scale)       nd.scale       = { node.scale[0], node.scale[1], node.scale[2] };
        }
        nd.parent = node.parent ? static_cast<int>(node.parent - data->nodes) : -1;
    }

    // ---- skin (first) ----
    if (data->skins_count > 0)
    {
        const cgltf_skin& skin = data->skins[0];
        out.skin.joints.resize(skin.joints_count);
        for (cgltf_size j = 0; j < skin.joints_count; ++j)
            out.skin.joints[j] = static_cast<int>(skin.joints[j] - data->nodes);

        // The default node pose is NOT necessarily the mesh's bind pose.
        // Preserve the authored inverse bind matrices; absent means identity.
        out.skin.inverseBind.resize(skin.joints_count);
        for (cgltf_size j = 0; j < skin.joints_count; ++j)
        {
            auto& matrix = out.skin.inverseBind[j];
            XMStoreFloat4x4(&matrix, XMMatrixIdentity());
            if (skin.inverse_bind_matrices)
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, &matrix._11, 16);
        }
    }

    // ---- animations ----
    AppendAnimations(data, out.animations);

    // Iterate the node hierarchy so we can bake each node's world transform.
    // Static meshes get their node transform baked (fixes Z-up assets like the
    // helmet); skinned meshes are left in mesh space for inverse-bind * jointGlobal.
    for (cgltf_size n = 0; n < data->nodes_count; ++n)
    {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;

        const bool skinned = (node.skin != nullptr);

        float wm[16];
        cgltf_node_transform_world(&node, wm);
        XMFLOAT4X4 tmp;
        memcpy(&tmp, wm, sizeof(wm));                 // cgltf gives column-major
        XMMATRIX world = XMLoadFloat4x4(&tmp);

        XMVECTOR det;
        XMMATRIX normalMat = XMMatrixTranspose(XMMatrixInverse(&det, world));

        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            ProcessPrimitive(data, node.mesh->primitives[p], world, normalMat, skinned, out);
    }

    cgltf_free(data);

    for (auto& prim : out.primitives)
    {
        if (!prim.hasSkin) continue;
        for (auto& v : prim.vertices)
        {
            float* w = &v.weights.x;
            float sum = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (v.joints[k] >= out.skin.joints.size() || !std::isfinite(w[k]) || w[k] < 0)
                {
                    LOG_ERROR("Invalid joint index or skin weight: %s", path.c_str());
                    return false;
                }
                sum += w[k];
            }
            if (sum <= 0 || !std::isfinite(sum)) return false;
            for (int k = 0; k < 4; ++k) w[k] /= sum;
        }
    }

    if (out.primitives.empty())
    {
        LOG_ERROR("no triangle primitives found in %s", path.c_str());
        return false;
    }
    return true;
}

} // namespace gltf
