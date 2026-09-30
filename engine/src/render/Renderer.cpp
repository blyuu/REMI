#include <remi/render/Renderer.hpp>
#include <remi/core/Log.hpp>
#include <remi/core/Lifetime.hpp>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <algorithm>
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
ComPtr<ID3DBlob> Compile(const RendererConfig& config, const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    const HRESULT result = config.shaderSource.empty()
        ? D3DCompileFromFile(config.shaderFile.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile, flags, 0, &code, &errors)
        : D3DCompile(config.shaderSource.data(), config.shaderSource.size(), "ResourceManager shader", nullptr, nullptr, entry, profile, flags, 0, &code, &errors);
    if (FAILED(result)) {
        const std::string details = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Shader file missing or unreadable";
        throw std::runtime_error(std::string(entry) + ": " + details);
    }
    return code;
}
}
struct DeviceOwnership { std::size_t meshes = 0; };
struct DrawConstants {
    Matrix4 mvp, world, lightMatrix;
    Vec3 direction; float intensity = 0;
    Vec3 color; float ambient = 0;
    float lit = 0, shadows = 0, bias = .0015f, padding = 0;
};
struct GpuQuerySet {
    ComPtr<ID3D11Query> disjoint, start, shadowEnd, colorEnd;
    bool pending = false;
    std::uint64_t sequence = 0;
};
static_assert(sizeof(DrawConstants) == 240);
struct Mesh::Impl {
    LifetimeToken lifetime{OwnedKind::Mesh};
    ComPtr<ID3D11Buffer> vertices, indices;
    std::shared_ptr<DeviceOwnership> ownership;
    ID3D11Device* owner = nullptr;
    UINT count = 0;
    Vec3 minimum, maximum;
    ~Impl() { if (ownership) --ownership->meshes; }
};
Mesh::Mesh() : impl_(std::make_unique<Impl>()) {}
Mesh::~Mesh() = default;
bool Mesh::IntersectsClip(const Matrix4& mvp) const noexcept {
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
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11Texture2D> backbuffer, depth;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11Texture2D> shadowTexture;
    ComPtr<ID3D11DepthStencilView> shadowDepth;
    ComPtr<ID3D11ShaderResourceView> shadowView;
    ComPtr<ID3D11SamplerState> shadowSampler;
    ComPtr<ID3D11RasterizerState> shadowRaster;
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
    void MakeTargets(unsigned w, unsigned h) {
        Check(swap->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "Get swap buffer");
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        Check(device->CreateRenderTargetView(backbuffer.Get(), &view, &rtv), "Create sRGB RTV");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Check(device->CreateTexture2D(&desc, nullptr, &depth), "Create depth texture");
        Check(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv), "Create DSV");
        width = w; height = h;
    }
};

Renderer::Renderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    ValidateSize(config.width, config.height);
    if (!config.nativeWindow || !IsWindow(static_cast<HWND>(config.nativeWindow))) throw std::invalid_argument("Renderer requires a live HWND");
    auto& r = *impl_; r.vsync = config.vsync;
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (config.requestDebug || config.requireDebug) flags |= D3D11_CREATE_DEVICE_DEBUG;
    const auto driver = config.useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, flags, requested, 1, D3D11_SDK_VERSION, &r.device, &obtained, &r.context);
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING && !config.requireDebug) {
        Log("D3D11 debug layer unavailable; retrying without validation layer");
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, driver, nullptr, flags, requested, 1, D3D11_SDK_VERSION, &r.device, &obtained, &r.context);
    }
    Check(result, "D3D11CreateDevice (feature level 11.0)");
    if (flags & D3D11_CREATE_DEVICE_DEBUG) Check(r.device.As(&r.diagnostics), "Query debug info queue");
    Log(std::string("D3D11 FL11.0 / ") + (config.useWarp ? "WARP" : "hardware") + (r.diagnostics ? " / debug ON" : " / debug OFF"));
    ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
    Check(r.device.As(&dxgiDevice), "Query DXGI device");
    Check(dxgiDevice->GetAdapter(&adapter), "Get adapter");
    DXGI_ADAPTER_DESC adapterDesc{}; Check(adapter->GetDesc(&adapterDesc), "Get adapter description");
    std::wstring adapterName(adapterDesc.Description);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, adapterName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes > 0) { std::string name(static_cast<std::size_t>(bytes), '\0'); WideCharToMultiByte(CP_UTF8, 0, adapterName.c_str(), -1, name.data(), bytes, nullptr, nullptr); name.pop_back(); Log("Adapter: " + name); }
    Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get DXGI factory");
    DXGI_SWAP_CHAIN_DESC1 swap{};
    swap.Width = config.width; swap.Height = config.height; swap.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1; swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; swap.BufferCount = 2;
    swap.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Check(factory->CreateSwapChainForHwnd(r.device.Get(), static_cast<HWND>(config.nativeWindow), &swap, nullptr, nullptr, &r.swap), "Create flip swap chain");
    Check(factory->MakeWindowAssociation(static_cast<HWND>(config.nativeWindow), DXGI_MWA_NO_ALT_ENTER), "Disable DXGI fullscreen shortcut");
    r.MakeTargets(config.width, config.height);
    const auto vertexCode = Compile(config, "VSMain", "vs_5_0");
    const auto pixelCode = Compile(config, "PSMain", "ps_5_0");
    Check(r.device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &r.vs), "Create VS");
    Check(r.device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &r.ps), "Create PS");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    static_assert(sizeof(Vertex) == 24 && offsetof(Vertex, color) == 12);
    Check(r.device->CreateInputLayout(elements, 2, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &r.layout), "Create vertex layout");
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = sizeof(DrawConstants); buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    static_assert(sizeof(Matrix4) == 64);
    Check(r.device->CreateBuffer(&buffer, nullptr, &r.constants), "Create per-object constants");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_BACK; raster.DepthClipEnable = TRUE;
    Check(r.device->CreateRasterizerState(&raster, &r.raster), "Create raster state");
    raster.DepthBias = 1000; raster.SlopeScaledDepthBias = 2; raster.DepthBiasClamp = .01f;
    Check(r.device->CreateRasterizerState(&raster,&r.shadowRaster),"Create shadow raster state");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
    Check(r.device->CreateDepthStencilState(&depth, &r.depthState), "Create depth state");
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
    if (impl_->closed) throw std::logic_error("Renderer is shut down");
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
        vertices.size_bytes() > std::numeric_limits<UINT>::max() || indices.size_bytes() > std::numeric_limits<UINT>::max())
        throw std::invalid_argument("Invalid triangle mesh size");
    for (auto index : indices) if (index >= vertices.size()) throw std::invalid_argument("Mesh index out of range");
    for (const auto& vertex : vertices)
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.color.x) || !std::isfinite(vertex.color.y) || !std::isfinite(vertex.color.z))
            throw std::invalid_argument("Mesh contains non-finite values");
    auto mesh = std::unique_ptr<Mesh>(new Mesh());
    mesh->impl_->minimum = mesh->impl_->maximum = vertices.front().position;
    for (const auto& vertex : vertices) {
        auto& lo = mesh->impl_->minimum; auto& hi = mesh->impl_->maximum; const auto p = vertex.position;
        lo = {std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi = {std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    auto create = [&](const void* data, UINT size, UINT bind, ComPtr<ID3D11Buffer>& target) {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = size; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = bind;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data;
        Check(impl_->device->CreateBuffer(&desc, &initial, &target), "Create mesh buffer");
    };
    create(vertices.data(), static_cast<UINT>(vertices.size_bytes()), D3D11_BIND_VERTEX_BUFFER, mesh->impl_->vertices);
    create(indices.data(), static_cast<UINT>(indices.size_bytes()), D3D11_BIND_INDEX_BUFFER, mesh->impl_->indices);
    mesh->impl_->count = static_cast<UINT>(indices.size()); mesh->impl_->owner = impl_->device.Get();
    mesh->impl_->ownership = impl_->ownership; ++impl_->ownership->meshes;
    return mesh;
}
void Renderer::Resize(unsigned width, unsigned height) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (!width || !height || (width == r.width && height == r.height)) return;
    ValidateSize(width, height);
    r.activeFrame = false;
    r.context->OMSetRenderTargets(0, nullptr, nullptr);
    r.rtv.Reset(); r.dsv.Reset(); r.backbuffer.Reset(); r.depth.Reset();
    Check(r.swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    r.MakeTargets(width, height);
}
void Renderer::BeginFrame(Vec3 color) {
    auto& r = *impl_;
    if (r.closed) throw std::logic_error("Renderer is shut down");
    if (r.shadowPass) throw std::logic_error("End shadow pass before BeginFrame");
    r.context->OMSetRenderTargets(1, r.rtv.GetAddressOf(), r.dsv.Get());
    const float clear[] = {color.x, color.y, color.z, 1};
    r.context->ClearRenderTargetView(r.rtv.Get(), clear);
    r.context->ClearDepthStencilView(r.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1};
    r.context->RSSetViewports(1, &viewport); r.context->RSSetState(r.raster.Get());
    r.context->OMSetDepthStencilState(r.depthState.Get(), 0); r.context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    r.context->IASetInputLayout(r.layout.Get()); r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.vs.Get(), nullptr, 0); r.context->PSSetShader(r.ps.Get(), nullptr, 0);
    r.context->VSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetConstantBuffers(0, 1, r.constants.GetAddressOf());
    r.context->PSSetShaderResources(0,1,r.shadowView.GetAddressOf());
    r.context->PSSetSamplers(0,1,r.shadowSampler.GetAddressOf());
    r.shadowPass = false;
    r.shadowReady = false;
    r.activeFrame = true;
}
void Renderer::Draw(const Mesh& mesh, const Matrix4& mvp) {
    DrawInternal(mesh,mvp,Matrix4::Identity(),nullptr);
}
void Renderer::BeginShadow(const Matrix4& matrix) {
    for (const auto value : matrix.values) if (!std::isfinite(value)) throw std::invalid_argument("Invalid light matrix");
    BeginFrame(); auto& r = *impl_; r.lightMatrix = matrix; r.shadowReady = false; r.shadowPass = true;
    ID3D11ShaderResourceView* empty = nullptr; r.context->PSSetShaderResources(0,1,&empty);
    r.context->OMSetRenderTargets(0,nullptr,r.shadowDepth.Get());
    r.context->ClearDepthStencilView(r.shadowDepth.Get(),D3D11_CLEAR_DEPTH,1,0);
    const D3D11_VIEWPORT viewport{0,0,2048,2048,0,1}; r.context->RSSetViewports(1,&viewport);
    r.context->RSSetState(r.shadowRaster.Get());
    r.context->PSSetShader(nullptr,nullptr,0);
}
void Renderer::EndShadow() {
    if (!impl_->shadowPass) throw std::logic_error("EndShadow requires shadow pass");
    if (impl_->gpuActive >= 0) impl_->context->End(impl_->gpuQueries[impl_->gpuActive].shadowEnd.Get());
    impl_->shadowPass = false; BeginFrame(); impl_->shadowReady = true;
}
void Renderer::DrawLit(const Mesh& mesh, const Matrix4& world, const Matrix4& viewProjection, const DirectionalLight& light) {
    const float length = light.direction.x*light.direction.x+light.direction.y*light.direction.y+light.direction.z*light.direction.z;
    if (!std::isfinite(length) || length < .00000001f || !std::isfinite(light.intensity) || light.intensity < 0 ||
        !std::isfinite(light.ambient) || light.ambient < 0 || !std::isfinite(light.color.x) || !std::isfinite(light.color.y) ||
        !std::isfinite(light.color.z) || light.color.x < 0 || light.color.y < 0 || light.color.z < 0)
        throw std::invalid_argument("Invalid directional light");
    if (impl_->shadowPass) throw std::logic_error("Lit draw inside shadow pass");
    DrawInternal(mesh,Multiply(world,viewProjection),world,&light);
}
void Renderer::DrawInternal(const Mesh& mesh, const Matrix4& mvp, const Matrix4& world, const DirectionalLight* light) {
    auto& r = *impl_;
    if (!r.activeFrame) throw std::logic_error("Draw requires BeginFrame");
    if (mesh.impl_->owner != r.device.Get()) throw std::invalid_argument("Mesh belongs to another device");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(r.context->Map(r.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map constants");
    DrawConstants constants{}; constants.mvp = mvp; constants.world = world; constants.lightMatrix = r.lightMatrix;
    if (light) { constants.direction = light->direction; constants.intensity = light->intensity; constants.color = light->color;
        constants.ambient = light->ambient; constants.lit = 1; constants.shadows = r.shadowReady ? 1.f : 0.f; }
    std::memcpy(mapped.pData, &constants, sizeof(constants)); r.context->Unmap(r.constants.Get(), 0);
    const UINT stride = sizeof(Vertex), offset = 0;
    r.context->IASetVertexBuffers(0, 1, mesh.impl_->vertices.GetAddressOf(), &stride, &offset);
    r.context->IASetIndexBuffer(mesh.impl_->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    r.context->DrawIndexed(mesh.impl_->count, 0, 0);
}
void Renderer::Present() {
    auto& r = *impl_;
    if (!r.activeFrame || r.shadowPass) throw std::logic_error("Present requires color pass");
    const HRESULT result = r.swap->Present(r.vsync ? 1 : 0, 0);
    r.activeFrame = false;
    if (result == DXGI_STATUS_OCCLUDED) Sleep(16);
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
        Check(r.device->GetDeviceRemovedReason(), "D3D11 device removed");
    Check(result, "Present");
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
    D3D11_TEXTURE2D_DESC desc{}; r.backbuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Check(r.device->CreateTexture2D(&desc, nullptr, &staging), "Create screenshot staging texture");
    r.context->CopyResource(staging.Get(), r.backbuffer.Get());
    FrameImage image{r.width, r.height, std::vector<std::uint8_t>(static_cast<std::size_t>(r.width) * r.height * 4)};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(r.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map screenshot");
    for (unsigned row = 0; row < r.height; ++row)
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(row) * r.width * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, static_cast<std::size_t>(r.width) * 4);
    r.context->Unmap(staging.Get(), 0); return image;
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
    r.constants.Reset(); r.layout.Reset(); r.vs.Reset(); r.ps.Reset(); r.raster.Reset(); r.depthState.Reset();
    r.shadowSampler.Reset(); r.shadowView.Reset(); r.shadowDepth.Reset(); r.shadowTexture.Reset(); r.shadowRaster.Reset();
    for (auto& set : r.gpuQueries) { set.disjoint.Reset(); set.start.Reset(); set.shadowEnd.Reset(); set.colorEnd.Reset(); }
    r.rtv.Reset(); r.dsv.Reset(); r.backbuffer.Reset(); r.depth.Reset(); r.swap.Reset(); r.context.Reset();
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
