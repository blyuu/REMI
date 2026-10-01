#include <remi/render/Renderer.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/Lifetime.hpp>
#include <remi/rhi/RHIFactory.hpp>
#include <remi/render/ShaderCache.hpp>
#include "../rhi/d3d11/D3D11RHI.hpp"
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace remi {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message; message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
        throw std::runtime_error(message.str());
    }
}
void ValidateSize(unsigned width, unsigned height) {
    if (!width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        throw std::invalid_argument("Invalid render target size");
}
}
struct DeviceOwnership { std::size_t meshes = 0; };
struct DrawConstants {
    Matrix4 mvp, world, lightMatrix;
    Vec3 direction; float intensity = 0;
    Vec3 color; float ambient = 0;
    float lit = 0, shadows = 0, bias = .0015f, padding = 0;
    Vec3 cameraPosition; float padding2 = 0;
    Vec3 materialTint{1,1,1}; float metallic = 0;
    float roughness = .6f; float emissive = 0; float materialPadding[2]{};
    float hasBaseColorTexture = 0; float texturePadding[3]{};
};
struct GpuQuerySet {
    ComPtr<ID3D11Query> disjoint, start, shadowEnd, colorEnd;
    bool pending = false;
    std::uint64_t sequence = 0;
};
static_assert(sizeof(DrawConstants) == 304);
struct Mesh::Impl {
    LifetimeToken lifetime{OwnedKind::Mesh};
    std::unique_ptr<rhi::IRHIBuffer> vertices, indices;
    std::unique_ptr<rhi::IRHITexture> baseColorTexture;
    std::shared_ptr<DeviceOwnership> ownership;
    ID3D11Device* owner = nullptr;
    UINT count = 0, vertexCount = 0;
    bool dynamic = false;
    Vec3 minimum, maximum;
    ~Impl() { if (ownership) --ownership->meshes; }
};
Mesh::Mesh() : impl_(std::make_unique<Impl>()) {}
Mesh::~Mesh() = default;
bool Mesh::IntersectsClip(const Matrix4& mvp) const noexcept {
    if (impl_->dynamic) return true; // Animated bounds change; do not cull with the bind-pose box.
    unsigned common = 63;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const float x = (corner & 1) ? impl_->maximum.x : impl_->minimum.x;
        const float y = (corner & 2) ? impl_->maximum.y : impl_->minimum.y;
        const float z = (corner & 4) ? impl_->maximum.z : impl_->minimum.z;
        const auto& m = mvp.values;
        const float a = x*m[0]+y*m[4]+z*m[8]+m[12], b = x*m[1]+y*m[5]+z*m[9]+m[13];
        const float c = x*m[2]+y*m[6]+z*m[10]+m[14], w = x*m[3]+y*m[7]+z*m[11]+m[15];
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(w)) return true;
        const unsigned mask = (a < -w ? 1u:0u) | (a > w ? 2u:0u) | (b < -w ? 4u:0u) |
            (b > w ? 8u:0u) | (c < 0 ? 16u:0u) | (c > w ? 32u:0u);
        common &= mask;
    }
    return common == 0;
}
Matrix4 DirectionalShadowMatrix(Vec3 center, Vec3 direction, float extent) {
    using namespace DirectX;
    const float length = std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    if (!std::isfinite(length) || length < .0001f || !std::isfinite(extent) || extent <= 0 ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) throw std::invalid_argument("Invalid shadow volume");
    const auto d = XMVectorSet(direction.x/length,direction.y/length,direction.z/length,0);
    const auto target = XMVectorSet(center.x,center.y,center.z,1);
    const auto eye = XMVectorSubtract(target,XMVectorScale(d,extent));
    const auto up = std::abs(direction.y/length) > .99f ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
    XMFLOAT4X4 matrix;
    XMStoreFloat4x4(&matrix,XMMatrixLookAtLH(eye,target,up)*XMMatrixOrthographicLH(extent,extent,.01f,extent*2));
    Matrix4 result; std::memcpy(result.values.data(),&matrix,sizeof(matrix)); return result;
}

struct Renderer::Impl {
    LifetimeToken lifetime{OwnedKind::Renderer};
    // Shared bookkeeping only: a Mesh can report ownership until its own destruction.
    std::shared_ptr<DeviceOwnership> ownership = std::make_shared<DeviceOwnership>();
    std::unique_ptr<rhi::IRHIDevice> rhiDevice;
    std::unique_ptr<rhi::IRHISurface> surface;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::array<std::unique_ptr<rhi::IRHIPipeline>, static_cast<std::size_t>(ShadingModel::Count)> pipelines;
    std::unique_ptr<rhi::IRHIPipeline> shadowPipeline;
    std::unique_ptr<ShaderCache> shaderCache;
    std::unique_ptr<rhi::IRHIBuffer> constants;
    ComPtr<ID3D11Texture2D> shadowTexture;
    ComPtr<ID3D11DepthStencilView> shadowDepth;
    ComPtr<ID3D11ShaderResourceView> shadowView;
    ComPtr<ID3D11SamplerState> shadowSampler;
    std::array<GpuQuerySet,4> gpuQueries;
    unsigned gpuNext = 0;
    int gpuActive = -1;
    GpuTimings lastGpuTimings;
    std::uint64_t gpuSequence = 0, lastGpuSequence = 0;
    Matrix4 lightMatrix = Matrix4::Identity();
    bool shadowPass = false, shadowReady = false;
    ComPtr<ID3D11InfoQueue> diagnostics;
    unsigned width = 0, height = 0;
    bool vsync = true, activeFrame = false;
    bool closed = false;
    ShutdownReport shutdownReport;
    ~Impl() { if (context) { context->ClearState(); context->Flush(); } }
    void BuildPipelines() {
        rhi::PipelineDesc desc;
        desc.vertexShader = shaderCache->Get({ShaderStage::Vertex});
        desc.inputs = {{"POSITION",0,rhi::VertexFormat::Float3}, {"COLOR",12,rhi::VertexFormat::Float3},
            {"NORMAL",24,rhi::VertexFormat::Float3}, {"TEXCOORD",36,rhi::VertexFormat::Float2}};
        std::array<std::unique_ptr<rhi::IRHIPipeline>, static_cast<std::size_t>(ShadingModel::Count)> next;
        for (std::size_t i = 0; i < next.size(); ++i) {
            desc.pixelShader = shaderCache->Get({ShaderStage::Pixel, static_cast<ShadingModel>(i)});
            next[i] = rhiDevice->CreatePipeline(desc);
        }
        desc.pixelShader.clear(); desc.depthBias = 1000; desc.slopeScaledDepthBias = 2;
        auto shadow = rhiDevice->CreatePipeline(desc);
        pipelines.swap(next); shadowPipeline.swap(shadow);
    }
};

Renderer::Renderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    ValidateSize(config.width, config.height);
    if (!config.nativeWindow || !IsWindow(static_cast<HWND>(config.nativeWindow))) throw std::invalid_argument("Renderer requires a live HWND");
    auto& r = *impl_; r.vsync = config.vsync;
    r.rhiDevice = rhi::CreateDevice({rhi::Backend::D3D11,
        config.useWarp ? rhi::Driver::Warp : rhi::Driver::Hardware, config.requestDebug, config.requireDebug});
    auto& backend = static_cast<rhi::D3D11Device&>(*r.rhiDevice);
    r.device = backend.NativeDevice();
    r.context = backend.NativeContext();
    if (backend.Info().debugLayer) Check(r.device.As(&r.diagnostics), "Query debug info queue");
    else if (config.requestDebug) Log("D3D11 debug layer unavailable; continuing without validation layer");
    Log(std::string("D3D11 FL11.0 / ") + (config.useWarp ? "WARP" : "hardware") + (r.diagnostics ? " / debug ON" : " / debug OFF"));
    Log("Adapter: " + backend.Info().adapterName);
    r.surface = r.rhiDevice->CreateSurface(config.nativeWindow,config.width,config.height);
    r.width = config.width; r.height = config.height;
    r.shaderCache = std::make_unique<ShaderCache>(config.shaderFile, config.shaderSource);
    static_assert(sizeof(Vertex) == 44 && offsetof(Vertex, color) == 12 && offsetof(Vertex, normal) == 24 && offsetof(Vertex, uv) == 36);
    r.BuildPipelines();
    static_assert(sizeof(Matrix4) == 64);
    r.constants = r.rhiDevice->CreateBuffer({sizeof(DrawConstants), rhi::BufferBind::Constant, true, nullptr});
    D3D11_TEXTURE2D_DESC shadow{}; shadow.Width = shadow.Height = 2048; shadow.MipLevels = shadow.ArraySize = 1;
    shadow.Format = DXGI_FORMAT_R32_TYPELESS; shadow.SampleDesc.Count = 1;
    shadow.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    Check(r.device->CreateTexture2D(&shadow,nullptr,&r.shadowTexture),"Create shadow map");
    D3D11_DEPTH_STENCIL_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_D32_FLOAT; sd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    Check(r.device->CreateDepthStencilView(r.shadowTexture.Get(),&sd,&r.shadowDepth),"Create shadow DSV");
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    Check(r.device->CreateShaderResourceView(r.shadowTexture.Get(),&sv,&r.shadowView),"Create shadow SRV");
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] = sampler.BorderColor[3] = 1;
    sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL; sampler.MaxLOD = D3D11_FLOAT32_MAX;
    Check(r.device->CreateSamplerState(&sampler,&r.shadowSampler),"Create shadow sampler");
    D3D11_QUERY_DESC query{};
    for (auto& set : r.gpuQueries) {
        query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        Check(r.device->CreateQuery(&query,&set.disjoint),"Create GPU disjoint query");
        query.Query = D3D11_QUERY_TIMESTAMP;
        Check(r.device->CreateQuery(&query,&set.start),"Create GPU start query");
        Check(r.device->CreateQuery(&query,&set.shadowEnd),"Create GPU shadow query");
        Check(r.device->CreateQuery(&query,&set.colorEnd),"Create GPU color query");
    }
}
Renderer::~Renderer() = default;

std::unique_ptr<Mesh> Renderer::CreateMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    return CreateMeshInternal(vertices,indices,false);
}
std::unique_ptr<Mesh> Renderer::CreateDynamicMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    return CreateMeshInternal(vertices,indices,true);
}
std::unique_ptr<Mesh> Renderer::CreateMeshInternal(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices, bool dynamic) {
    if (impl_->closed) throw std::logic_error("Renderer is shut down");
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
        vertices.size_bytes() > std::numeric_limits<UINT>::max() || indices.size_bytes() > std::numeric_limits<UINT>::max())
        throw std::invalid_argument("Invalid triangle mesh size");
    for (auto index : indices) if (index >= vertices.size()) throw std::invalid_argument("Mesh index out of range");
    for (const auto& vertex : vertices)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.color.x) || !std::isfinite(vertex.color.y) || !std::isfinite(vertex.color.z) ||
            !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z))
            throw std::invalid_argument("Mesh contains non-finite values");
    auto mesh = std::unique_ptr<Mesh>(new Mesh());
    mesh->impl_->minimum = mesh->impl_->maximum = vertices.front().position;
    for (const auto& vertex : vertices) {
        auto& lo = mesh->impl_->minimum; auto& hi = mesh->impl_->maximum; const auto p = vertex.position;
        lo = {std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi = {std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    mesh->impl_->vertices = impl_->rhiDevice->CreateBuffer({vertices.size_bytes(), rhi::BufferBind::Vertex, dynamic, vertices.data()});
    mesh->impl_->indices = impl_->rhiDevice->CreateBuffer({indices.size_bytes(), rhi::BufferBind::Index, false, indices.data()});
    mesh->impl_->count = static_cast<UINT>(indices.size()); mesh->impl_->vertexCount = static_cast<UINT>(vertices.size());
    mesh->impl_->dynamic = dynamic; mesh->impl_->owner = impl_->device.Get();
    mesh->impl_->ownership = impl_->ownership; ++impl_->ownership->meshes;
    return mesh;
}
void Renderer::UpdateMeshVertices(Mesh& mesh, std::span<const Vertex> vertices) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (mesh.impl_->owner != r.device.Get() || !mesh.impl_->dynamic || vertices.size() != mesh.impl_->vertexCount)
        throw std::invalid_argument("Invalid dynamic mesh update");
    for (const auto& vertex : vertices)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.color.x) || !std::isfinite(vertex.color.y) || !std::isfinite(vertex.color.z) ||
            !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z))
            throw std::invalid_argument("Dynamic mesh contains non-finite values");
    r.rhiDevice->UpdateBuffer(*mesh.impl_->vertices, vertices.data(), vertices.size_bytes());
}
void Renderer::SetMeshTexture(Mesh& mesh, unsigned width, unsigned height, std::span<const std::uint8_t> rgba, bool srgb) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (mesh.impl_->owner != r.device.Get() || !width || !height ||
        static_cast<std::uint64_t>(width) * height * 4 != rgba.size())
        throw std::invalid_argument("Invalid mesh texture");
    mesh.impl_->baseColorTexture = r.rhiDevice->CreateTexture({width, height,
        srgb ? rhi::TextureFormat::RGBA8Srgb : rhi::TextureFormat::RGBA8, false, rgba.data()});
}
void Renderer::Resize(unsigned width, unsigned height) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (!width || !height || (width == r.width && height == r.height)) return;
    ValidateSize(width, height);
    r.activeFrame = false;
    r.surface->Resize(width,height);
    r.width = width; r.height = height;
}
void Renderer::BeginFrame(Vec3 color) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (r.shadowPass) throw std::logic_error("End shadow pass before BeginFrame");
    const float clear[] = {color.x, color.y, color.z, 1};
    r.surface->BeginColorPass(clear);
    auto& commands = r.rhiDevice->Context();
    commands.BeginExternalPass();
    commands.SetPipeline(*r.pipelines[0]);
    commands.SetConstantBuffer(0, *r.constants);
    r.context->PSSetShaderResources(0,1,r.shadowView.GetAddressOf());
    r.context->PSSetSamplers(0,1,r.shadowSampler.GetAddressOf());
    r.shadowPass = false;
    r.shadowReady = false;
    r.activeFrame = true;
}
void Renderer::Draw(const Mesh& mesh, const Matrix4& mvp) {
    DrawInternal(mesh,mvp,Matrix4::Identity(),nullptr,{});
}
void Renderer::BeginShadow(const Matrix4& matrix) {
    for (const auto value : matrix.values) if (!std::isfinite(value)) throw std::invalid_argument("Invalid light matrix");
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (r.shadowPass) throw std::logic_error("Shadow pass already active");
    r.lightMatrix = matrix; r.shadowReady = false;
    ID3D11ShaderResourceView* empty = nullptr; r.context->PSSetShaderResources(0,1,&empty);
    r.context->OMSetRenderTargets(0,nullptr,r.shadowDepth.Get());
    r.context->ClearDepthStencilView(r.shadowDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
    const D3D11_VIEWPORT viewport{0,0,2048,2048,0,1}; r.context->RSSetViewports(1,&viewport);
    r.context->OMSetBlendState(nullptr,nullptr,0xffffffff);
    auto& commands = r.rhiDevice->Context();
    commands.BeginExternalPass();
    commands.SetPipeline(*r.shadowPipeline);
    commands.SetConstantBuffer(0,*r.constants);
    r.activeFrame = true; r.shadowPass = true;
}
void Renderer::EndShadow() {
    if (!impl_->shadowPass) throw std::logic_error("EndShadow requires shadow pass");
    if (impl_->gpuActive >= 0) impl_->context->End(impl_->gpuQueries[impl_->gpuActive].shadowEnd.Get());
    impl_->rhiDevice->Context().EndExternalPass();
    impl_->shadowPass = false; BeginFrame(); impl_->shadowReady = true;
}
void Renderer::DrawLit(const Mesh& mesh, const Matrix4& world, const Matrix4& viewProjection, const DirectionalLight& light, const MaterialProperties& material) {
    DrawLitPrepared(mesh,world,Multiply(world,viewProjection),light,material);
}
void Renderer::DrawLitPrepared(const Mesh& mesh, const Matrix4& world, const Matrix4& modelViewProjection, const DirectionalLight& light, const MaterialProperties& material) {
    const float length = light.direction.x*light.direction.x+light.direction.y*light.direction.y+light.direction.z*light.direction.z;
    if (!std::isfinite(length) || length < .00000001f || !std::isfinite(light.intensity) || light.intensity < 0 ||
        !std::isfinite(light.ambient) || light.ambient < 0 || !std::isfinite(light.color.x) || !std::isfinite(light.color.y) ||
        !std::isfinite(light.color.z) || light.color.x < 0 || light.color.y < 0 || light.color.z < 0 ||
        !std::isfinite(light.cameraPosition.x) || !std::isfinite(light.cameraPosition.y) || !std::isfinite(light.cameraPosition.z))
        throw std::invalid_argument("Invalid directional light");
    if (!std::isfinite(material.tint.x) || !std::isfinite(material.tint.y) || !std::isfinite(material.tint.z) ||
        material.tint.x < 0 || material.tint.y < 0 || material.tint.z < 0 ||
        !std::isfinite(material.metallic) || material.metallic < 0 || material.metallic > 1 ||
        !std::isfinite(material.roughness) || material.roughness < .04f || material.roughness > 1 ||
        !std::isfinite(material.emissive) || material.emissive < 0 || material.emissive > 2 ||
        material.shadingModel >= ShadingModel::Count)
        throw std::invalid_argument("Invalid material properties");
    if (impl_->shadowPass) throw std::logic_error("Lit draw inside shadow pass");
    DrawInternal(mesh,modelViewProjection,world,&light,material);
}
void Renderer::DrawInternal(const Mesh& mesh, const Matrix4& mvp, const Matrix4& world, const DirectionalLight* light, const MaterialProperties& material) {
    auto& r = *impl_;
    if (!r.activeFrame) throw std::logic_error("Draw requires BeginFrame");
    if (mesh.impl_->owner != r.device.Get()) throw std::invalid_argument("Mesh belongs to another device");
    auto& commands = r.rhiDevice->Context();
    if (!r.shadowPass) commands.SetPipeline(*r.pipelines[static_cast<std::size_t>(light ? material.shadingModel : ShadingModel::Standard)]);
    DrawConstants constants{}; constants.mvp = mvp; constants.world = world; constants.lightMatrix = r.lightMatrix;
    if (light) { constants.direction = light->direction; constants.intensity = light->intensity; constants.color = light->color;
        constants.ambient = light->ambient; constants.cameraPosition = light->cameraPosition;
        constants.lit = 1; constants.shadows = r.shadowReady ? 1.f : 0.f; }
    constants.materialTint = material.tint; constants.metallic = material.metallic;
    constants.roughness = material.roughness; constants.emissive = material.emissive;
    constants.hasBaseColorTexture = mesh.impl_->baseColorTexture ? 1.f : 0.f;
    r.rhiDevice->UpdateBuffer(*r.constants, &constants, sizeof(constants));
    if (!r.shadowPass) commands.SetTexture(1,mesh.impl_->baseColorTexture.get());
    commands.SetVertexBuffer(*mesh.impl_->vertices,sizeof(Vertex));
    commands.SetIndexBuffer(*mesh.impl_->indices);
    commands.DrawIndexed(mesh.impl_->count);
}
void Renderer::Present() {
    auto& r = *impl_;
    if (!r.activeFrame || r.shadowPass) throw std::logic_error("Present requires color pass");
    r.rhiDevice->Context().EndExternalPass();
    r.surface->Present(r.vsync);
    r.activeFrame = false;
}
bool Renderer::ReloadShaders() {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    auto previous = *r.shaderCache;
    if (!r.shaderCache->ReloadIfChanged()) return false;
    try { r.BuildPipelines(); }
    catch (...) { *r.shaderCache = std::move(previous); throw; }
    return true;
}
bool Renderer::ReplaceShaderSource(std::string source) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    auto previous = *r.shaderCache;
    if (!r.shaderCache->ReplaceSource(std::move(source))) return false;
    try { r.BuildPipelines(); }
    catch (...) { *r.shaderCache = std::move(previous); throw; }
    return true;
}
void Renderer::BeginGpuProfile() {
    auto& r = *impl_;
    if (r.closed || r.gpuActive >= 0) throw std::logic_error("GPU profile already active or renderer closed");
    auto& set = r.gpuQueries[r.gpuNext];
    if (set.pending) return; // GPU is behind; skip a sample without blocking.
    r.gpuActive = static_cast<int>(r.gpuNext);
    r.gpuNext = (r.gpuNext+1) % static_cast<unsigned>(r.gpuQueries.size());
    r.context->Begin(set.disjoint.Get()); r.context->End(set.start.Get());
}
void Renderer::EndGpuProfile() {
    auto& r = *impl_;
    if (r.gpuActive < 0) return; // Begin skipped a busy query slot.
    auto& set = r.gpuQueries[r.gpuActive];
    r.context->End(set.colorEnd.Get()); r.context->End(set.disjoint.Get());
    set.pending = true; set.sequence = ++r.gpuSequence; r.gpuActive = -1;
}
GpuTimings Renderer::PollGpuProfile() {
    auto& r = *impl_;
    if (r.closed) return {};
    for (auto& set : r.gpuQueries) if (set.pending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 start = 0, shadow = 0, color = 0;
        constexpr UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
        if (r.context->GetData(set.disjoint.Get(),&disjoint,sizeof(disjoint),flags) != S_OK ||
            r.context->GetData(set.start.Get(),&start,sizeof(start),flags) != S_OK ||
            r.context->GetData(set.shadowEnd.Get(),&shadow,sizeof(shadow),flags) != S_OK ||
            r.context->GetData(set.colorEnd.Get(),&color,sizeof(color),flags) != S_OK) continue;
        set.pending = false;
        if (set.sequence > r.lastGpuSequence && !disjoint.Disjoint && disjoint.Frequency && start <= shadow && shadow <= color) {
            const double scale = 1000.0/static_cast<double>(disjoint.Frequency);
            r.lastGpuTimings = {true,(shadow-start)*scale,(color-shadow)*scale,(color-start)*scale,set.sequence};
            r.lastGpuSequence = set.sequence;
        }
    }
    return r.lastGpuTimings;
}
FrameImage Renderer::Readback() {
    auto& r = *impl_;
    if (!r.activeFrame || r.shadowPass) throw std::logic_error("Readback requires color pass before Present");
    auto readback = r.surface->Readback();
    return {readback.width,readback.height,std::move(readback.rgba)};
}
void Renderer::SaveScreenshot(const std::filesystem::path& path) {
    auto image = Readback();
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(image.rgba.size());
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(image.width); info.biHeight = -static_cast<LONG>(image.height);
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) std::swap(image.rgba[i], image.rgba[i + 2]);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
    if (!output) throw std::runtime_error("Could not save screenshot");
}
bool Renderer::DebugLayerEnabled() const noexcept { return impl_->diagnostics != nullptr; }
unsigned Renderer::CheckDiagnostics() {
    if (!impl_->diagnostics) return 0;
    auto& queue = impl_->diagnostics;
    unsigned count = 0;
    for (UINT64 i = 0; i < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0; Check(queue->GetMessage(i, nullptr, &size), "Read debug message size");
        std::vector<std::uint8_t> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Check(queue->GetMessage(i, message, &size), "Read debug message");
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) { ++count; Log(message->pDescription); }
    }
    queue->ClearStoredMessages(); return count;
}
unsigned Renderer::Width() const noexcept { return impl_->width; }
unsigned Renderer::Height() const noexcept { return impl_->height; }
std::size_t Renderer::LiveMeshes() const noexcept { return impl_->ownership->meshes; }
ShutdownReport Renderer::ShutdownAndValidate() {
    auto& r = *impl_;
    if (r.closed) return r.shutdownReport;
    if (LiveMeshes()) throw std::logic_error("Release Mesh owners before Renderer shutdown");
    r.shutdownReport.priorWarnings = CheckDiagnostics();
    ComPtr<ID3D11Debug> debug;
    if (r.diagnostics) Check(r.device.As(&debug), "Query shutdown debug interface");
    r.context->ClearState(); r.context->Flush();
    r.constants.reset();
    for (auto& pipeline : r.pipelines) pipeline.reset();
    r.shadowPipeline.reset();
    r.shaderCache.reset();
    r.shadowSampler.Reset(); r.shadowView.Reset(); r.shadowDepth.Reset(); r.shadowTexture.Reset();
    for (auto& set : r.gpuQueries) { set.disjoint.Reset(); set.start.Reset(); set.shadowEnd.Reset(); set.colorEnd.Reset(); }
    r.surface.reset(); r.context.Reset();
    r.rhiDevice.reset();
    r.activeFrame = false; r.closed = true;
    if (debug) {
        r.diagnostics->ClearStoredMessages();
        Check(debug->ReportLiveDeviceObjects(static_cast<D3D11_RLDO_FLAGS>(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL)), "Report live D3D objects");
        r.shutdownReport.debugValidated = true;
        for (UINT64 i = 0; i < r.diagnostics->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0; Check(r.diagnostics->GetMessage(i,nullptr,&size), "Read live message size");
            std::vector<std::uint8_t> data(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(data.data());
            Check(r.diagnostics->GetMessage(i,message,&size), "Read live message");
            // ReportLiveDeviceObjects emits the device itself, which we intentionally hold to query it.
            // All other live-object detail messages are unexpected engine-owned children.
            const std::string_view description(message->pDescription, message->DescriptionByteLength);
            if (message->ID != D3D11_MESSAGE_ID_LIVE_DEVICE && message->ID != D3D11_MESSAGE_ID_LIVE_OBJECT_SUMMARY &&
                description.find("Live ") != std::string_view::npos) {
                ++r.shutdownReport.liveChildren; Log(description);
            }
        }
        r.diagnostics->ClearStoredMessages();
    }
    debug.Reset(); r.diagnostics.Reset(); r.device.Reset();
    return r.shutdownReport;
}
}
