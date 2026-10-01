#include "D3D11RHI.hpp"
#include <dxgi.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <d3dcompiler.h>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <utility>
#include <array>
#include <algorithm>

namespace remi::rhi {
namespace {
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
        throw std::runtime_error(message.str());
    }
}
std::string Utf8(const wchar_t* text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back();
    return result;
}
class Texture final : public IRHITexture {
public:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    ID3D11Device* owner = nullptr;
    unsigned width = 0, height = 0;
    unsigned Width() const noexcept override { return width; }
    unsigned Height() const noexcept override { return height; }
};
class RenderTarget final : public IRHIRenderTarget {
public:
    std::unique_ptr<Texture> color;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    ID3D11Device* owner = nullptr;
    unsigned Width() const noexcept override { return color->Width(); }
    unsigned Height() const noexcept override { return color->Height(); }
    const IRHITexture& Color() const noexcept override { return *color; }
};
class Pipeline final : public IRHIPipeline {
public:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
    ID3D11Device* owner = nullptr;
};
class D3D11CommandContext final : public IRHIContext {
public:
    D3D11CommandContext(ID3D11Device* device, ID3D11DeviceContext* native) : device_(device), native_(native) {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        Check(device_->CreateSamplerState(&desc, &sampler_), "Create texture sampler");
    }
    void BeginExternalPass() override { active_ = true; external_ = true; }
    void EndExternalPass() override {
        if (!active_ || !external_) throw std::logic_error("No external RHI pass");
        active_ = false; external_ = false;
    }
    void BeginPass(IRHIRenderTarget& target, const float clear[4]) override {
        auto* t = dynamic_cast<RenderTarget*>(&target);
        if (!t || t->owner != device_.Get() || !clear || active_) throw std::invalid_argument("Invalid RHI render pass");
        ID3D11ShaderResourceView* none = nullptr;
        for (UINT slot = 0; slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++slot)
            native_->PSSetShaderResources(slot, 1, &none);
        native_->OMSetRenderTargets(1, t->rtv.GetAddressOf(), t->dsv.Get());
        native_->ClearRenderTargetView(t->rtv.Get(), clear);
        if (t->dsv) native_->ClearDepthStencilView(t->dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(t->Width()), static_cast<float>(t->Height()), 0, 1};
        native_->RSSetViewports(1, &viewport);
        active_ = true; external_ = false;
    }
    void EndPass() override {
        if (!active_ || external_) throw std::logic_error("No owned RHI pass");
        native_->OMSetRenderTargets(0, nullptr, nullptr);
        active_ = false;
    }
    void SetPipeline(const IRHIPipeline& pipeline) override {
        auto* p = dynamic_cast<const Pipeline*>(&pipeline);
        if (!p || p->owner != device_.Get()) throw std::invalid_argument("Pipeline belongs to another RHI device");
        native_->VSSetShader(p->vs.Get(), nullptr, 0);
        native_->PSSetShader(p->ps.Get(), nullptr, 0);
        native_->IASetInputLayout(p->layout.Get());
        native_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        native_->RSSetState(p->raster.Get());
        native_->OMSetDepthStencilState(p->depth.Get(), 0);
    }
    void SetVertexBuffer(const IRHIBuffer& buffer, unsigned stride) override {
        auto* b = dynamic_cast<const D3D11Buffer*>(&buffer);
        if (!b || b->Owner() != device_.Get() || !stride) throw std::invalid_argument("Invalid RHI vertex buffer");
        auto* native = b->NativeBuffer(); const UINT offset = 0;
        native_->IASetVertexBuffers(0, 1, &native, &stride, &offset);
    }
    void SetIndexBuffer(const IRHIBuffer& buffer) override {
        auto* b = dynamic_cast<const D3D11Buffer*>(&buffer);
        if (!b || b->Owner() != device_.Get()) throw std::invalid_argument("Invalid RHI index buffer");
        native_->IASetIndexBuffer(b->NativeBuffer(), DXGI_FORMAT_R32_UINT, 0);
    }
    void SetConstantBuffer(unsigned slot, const IRHIBuffer& buffer) override {
        auto* b = dynamic_cast<const D3D11Buffer*>(&buffer);
        if (!b || b->Owner() != device_.Get() || slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
            throw std::invalid_argument("Invalid RHI constant buffer");
        auto* native = b->NativeBuffer();
        native_->VSSetConstantBuffers(slot, 1, &native);
        native_->PSSetConstantBuffers(slot, 1, &native);
    }
    void SetTexture(unsigned slot, const IRHITexture* texture) override {
        if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) throw std::invalid_argument("Invalid RHI texture slot");
        auto* t = dynamic_cast<const Texture*>(texture);
        if (texture && (!t || t->owner != device_.Get())) throw std::invalid_argument("Texture belongs to another RHI device");
        ID3D11ShaderResourceView* view = t ? t->view.Get() : nullptr;
        native_->PSSetShaderResources(slot, 1, &view);
        native_->PSSetSamplers(slot, 1, sampler_.GetAddressOf());
    }
    void DrawIndexed(unsigned count) override {
        if (!active_ || !count) throw std::logic_error("DrawIndexed requires an active pass and indices");
        native_->DrawIndexed(count, 0, 0);
    }
    TextureReadback Readback(const IRHIRenderTarget& target) override {
        auto* t = dynamic_cast<const RenderTarget*>(&target);
        if (!t || t->owner != device_.Get() || active_) throw std::invalid_argument("Invalid RHI target readback");
        D3D11_TEXTURE2D_DESC desc{}; t->color->image->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Check(device_->CreateTexture2D(&desc, nullptr, &staging), "Create RHI readback staging texture");
        native_->CopyResource(staging.Get(), t->color->image.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(native_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map RHI readback texture");
        TextureReadback result{t->Width(), t->Height(),
            std::vector<std::uint8_t>(static_cast<std::size_t>(t->Width()) * t->Height() * 4)};
        for (unsigned row = 0; row < t->Height(); ++row)
            std::memcpy(result.rgba.data() + static_cast<std::size_t>(row) * t->Width() * 4,
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
                static_cast<std::size_t>(t->Width()) * 4);
        native_->Unmap(staging.Get(), 0);
        return result;
    }
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> native_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    bool active_ = false;
    bool external_ = false;
};
class D3D11Surface final : public IRHISurface {
public:
    D3D11Surface(ID3D11Device* device, ID3D11DeviceContext* context, void* window, unsigned width, unsigned height)
        : device_(device), context_(context) {
        if (!window || !IsWindow(static_cast<HWND>(window))) throw std::invalid_argument("Surface requires a live HWND");
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        Check(device_.As(&dxgi), "Query DXGI device");
        Check(dxgi->GetAdapter(&adapter), "Get adapter");
        Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width; desc.Height = height; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Check(factory->CreateSwapChainForHwnd(device_.Get(), static_cast<HWND>(window), &desc, nullptr, nullptr, &swap_), "Create swap chain");
        Check(factory->MakeWindowAssociation(static_cast<HWND>(window), DXGI_MWA_NO_ALT_ENTER), "Disable DXGI fullscreen shortcut");
        MakeTargets(width,height);
    }
    void Resize(unsigned width, unsigned height) override {
        if (!width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::invalid_argument("Invalid surface size");
        context_->OMSetRenderTargets(0,nullptr,nullptr);
        rtv_.Reset(); dsv_.Reset(); backbuffer_.Reset(); depth_.Reset();
        Check(swap_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0), "Resize swap chain");
        MakeTargets(width,height);
    }
    void BeginColorPass(const float clear[4]) override {
        if (!clear) throw std::invalid_argument("Missing clear color");
        context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
        context_->ClearRenderTargetView(rtv_.Get(),clear);
        context_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,1,0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    }
    void Present(bool vsync) override {
        const HRESULT result = swap_->Present(vsync ? 1 : 0,0);
        if (result == DXGI_STATUS_OCCLUDED) { Sleep(16); return; }
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
            Check(device_->GetDeviceRemovedReason(), "D3D11 device removed");
        Check(result,"Present");
    }
    TextureReadback Readback() override {
        D3D11_TEXTURE2D_DESC desc{}; backbuffer_->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Check(device_->CreateTexture2D(&desc,nullptr,&staging),"Create screenshot staging texture");
        context_->CopyResource(staging.Get(),backbuffer_.Get());
        TextureReadback result{width_,height_,std::vector<std::uint8_t>(static_cast<std::size_t>(width_)*height_*4)};
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map screenshot");
        for (unsigned row=0;row<height_;++row)
            std::memcpy(result.rgba.data()+static_cast<std::size_t>(row)*width_*4,
                static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(row)*mapped.RowPitch,
                static_cast<std::size_t>(width_)*4);
        context_->Unmap(staging.Get(),0);
        return result;
    }
private:
    void MakeTargets(unsigned width,unsigned height) {
        Check(swap_->GetBuffer(0,IID_PPV_ARGS(&backbuffer_)),"Get swap buffer");
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        Check(device_->CreateRenderTargetView(backbuffer_.Get(),&view,&rtv_),"Create sRGB RTV");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Check(device_->CreateTexture2D(&desc,nullptr,&depth_),"Create depth texture");
        Check(device_->CreateDepthStencilView(depth_.Get(),nullptr,&dsv_),"Create DSV");
        width_ = width; height_ = height;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer_,depth_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
    unsigned width_ = 0,height_ = 0;
};
}
D3D11Buffer::D3D11Buffer(Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, std::size_t size,
                         bool dynamic, ID3D11Device* owner)
    : buffer_(std::move(buffer)), size_(size), dynamic_(dynamic), owner_(owner) {}

D3D11Device::D3D11Device(const DeviceDesc& desc) {
    info_.driver = desc.driver;
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (desc.requestDebug || desc.requireDebug) flags |= D3D11_CREATE_DEVICE_DEBUG;
    const auto driver = desc.driver == Driver::Warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, flags, requested, 1,
                                       D3D11_SDK_VERSION, &device_, &obtained, &context_);
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING && !desc.requireDebug) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, driver, nullptr, flags, requested, 1,
                                   D3D11_SDK_VERSION, &device_, &obtained, &context_);
    }
    Check(result, "D3D11CreateDevice (feature level 11.0)");
    info_.debugLayer = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Check(device_.As(&dxgiDevice), "Query DXGI device");
    Check(dxgiDevice->GetAdapter(&adapter), "Get adapter");
    DXGI_ADAPTER_DESC adapterDesc{};
    Check(adapter->GetDesc(&adapterDesc), "Get adapter description");
    info_.adapterName = Utf8(adapterDesc.Description);
    contextApi_ = std::make_unique<D3D11CommandContext>(device_.Get(), context_.Get());
}

std::unique_ptr<IRHIBuffer> D3D11Device::CreateBuffer(const BufferDesc& desc) {
    if (desc.byteSize == 0 || desc.byteSize > std::numeric_limits<UINT>::max() ||
        (!desc.dynamic && !desc.initialData) ||
        (desc.bind == BufferBind::Constant && desc.byteSize % 16 != 0))
        throw std::invalid_argument("Invalid RHI buffer description");
    D3D11_BUFFER_DESC native{};
    native.ByteWidth = static_cast<UINT>(desc.byteSize);
    native.Usage = desc.dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
    native.CPUAccessFlags = desc.dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    switch (desc.bind) {
    case BufferBind::Vertex: native.BindFlags = D3D11_BIND_VERTEX_BUFFER; break;
    case BufferBind::Index: native.BindFlags = D3D11_BIND_INDEX_BUFFER; break;
    case BufferBind::Constant: native.BindFlags = D3D11_BIND_CONSTANT_BUFFER; break;
    default: throw std::invalid_argument("Unsupported RHI buffer bind type");
    }
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = desc.initialData;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Check(device_->CreateBuffer(&native, desc.initialData ? &initial : nullptr, &buffer), "Create RHI buffer");
    return std::make_unique<D3D11Buffer>(std::move(buffer), desc.byteSize, desc.dynamic, device_.Get());
}

void D3D11Device::UpdateBuffer(IRHIBuffer& buffer, const void* data, std::size_t bytes) {
    auto* native = dynamic_cast<D3D11Buffer*>(&buffer);
    if (!native || native->Owner() != device_.Get() || !native->Dynamic() ||
        !data || bytes != native->ByteSize())
        throw std::invalid_argument("Invalid RHI buffer update");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context_->Map(native->NativeBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map RHI buffer");
    std::memcpy(mapped.pData, data, bytes);
    context_->Unmap(native->NativeBuffer(), 0);
}

std::unique_ptr<IRHITexture> D3D11Device::CreateTexture(const TextureDesc& desc) {
    if (!desc.width || !desc.height || desc.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        desc.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        (desc.format != TextureFormat::Depth32 && !desc.renderTarget && !desc.pixels) ||
        (desc.format == TextureFormat::Depth32 && desc.pixels))
        throw std::invalid_argument("Invalid RHI texture description");
    auto texture = std::make_unique<Texture>();
    texture->owner = device_.Get(); texture->width = desc.width; texture->height = desc.height;
    D3D11_TEXTURE2D_DESC native{};
    native.Width = desc.width; native.Height = desc.height; native.MipLevels = native.ArraySize = 1;
    native.SampleDesc.Count = 1; native.Usage = D3D11_USAGE_DEFAULT;
    native.Format = desc.format == TextureFormat::Depth32 ? DXGI_FORMAT_R32_TYPELESS :
        desc.format == TextureFormat::RGBA8Srgb ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
    native.BindFlags = D3D11_BIND_SHADER_RESOURCE | (desc.renderTarget ? D3D11_BIND_RENDER_TARGET : 0);
    if (desc.format == TextureFormat::Depth32) native.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = desc.pixels; initial.SysMemPitch = desc.width * 4;
    Check(device_->CreateTexture2D(&native, desc.pixels ? &initial : nullptr, &texture->image), "Create RHI texture");
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.format == TextureFormat::Depth32 ? DXGI_FORMAT_R32_FLOAT :
        desc.format == TextureFormat::RGBA8Srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
    Check(device_->CreateShaderResourceView(texture->image.Get(), &srv, &texture->view), "Create RHI texture view");
    return texture;
}

std::unique_ptr<IRHIRenderTarget> D3D11Device::CreateRenderTarget(const RenderTargetDesc& desc) {
    if (!desc.width || !desc.height) throw std::invalid_argument("Invalid RHI render target size");
    auto target = std::make_unique<RenderTarget>();
    target->owner = device_.Get();
    auto color = CreateTexture({desc.width, desc.height, TextureFormat::RGBA8, true, nullptr});
    target->color.reset(static_cast<Texture*>(color.release()));
    Check(device_->CreateRenderTargetView(target->color->image.Get(), nullptr, &target->rtv), "Create RHI color target");
    if (desc.depth) {
        D3D11_TEXTURE2D_DESC depth{}; depth.Width = desc.width; depth.Height = desc.height;
        depth.MipLevels = depth.ArraySize = 1; depth.SampleDesc.Count = 1;
        depth.Format = DXGI_FORMAT_D32_FLOAT; depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Check(device_->CreateTexture2D(&depth, nullptr, &target->depth), "Create RHI depth target");
        Check(device_->CreateDepthStencilView(target->depth.Get(), nullptr, &target->dsv), "Create RHI depth view");
    }
    return target;
}

std::unique_ptr<IRHIPipeline> D3D11Device::CreatePipeline(const PipelineDesc& desc) {
    if (desc.vertexShader.empty() || desc.inputs.empty())
        throw std::invalid_argument("Invalid RHI pipeline description");
    auto pipeline = std::make_unique<Pipeline>(); pipeline->owner = device_.Get();
    Check(device_->CreateVertexShader(desc.vertexShader.data(), desc.vertexShader.size(), nullptr, &pipeline->vs), "Create RHI vertex shader");
    if (!desc.pixelShader.empty())
        Check(device_->CreatePixelShader(desc.pixelShader.data(), desc.pixelShader.size(), nullptr, &pipeline->ps), "Create RHI pixel shader");
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflected;
    Check(D3DReflect(desc.vertexShader.data(), desc.vertexShader.size(), IID_PPV_ARGS(&reflected)), "Reflect RHI vertex shader");
    D3D11_SHADER_DESC signature{};
    Check(reflected->GetDesc(&signature), "Read RHI vertex signature");
    std::vector<D3D11_INPUT_ELEMENT_DESC> inputs; inputs.reserve(signature.InputParameters);
    for (unsigned i = 0; i < signature.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC parameter{};
        Check(reflected->GetInputParameterDesc(i, &parameter), "Read RHI vertex input");
        const auto match = std::find_if(desc.inputs.begin(), desc.inputs.end(), [&](const InputElement& element) {
            return parameter.SemanticIndex == 0 && _stricmp(parameter.SemanticName, element.semantic.c_str()) == 0;
        });
        if (match == desc.inputs.end() || match->semantic.empty())
            throw std::invalid_argument(std::string("No vertex format mapping for shader input ") + parameter.SemanticName);
        const auto& element = *match;
        DXGI_FORMAT format = element.format == VertexFormat::Float2 ? DXGI_FORMAT_R32G32_FLOAT :
            element.format == VertexFormat::Float3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        inputs.push_back({element.semantic.c_str(), 0, format, 0, element.offset, D3D11_INPUT_PER_VERTEX_DATA, 0});
    }
    if (inputs.empty()) throw std::invalid_argument("Vertex shader has no mapped inputs");
    Check(device_->CreateInputLayout(inputs.data(), static_cast<UINT>(inputs.size()),
        desc.vertexShader.data(), desc.vertexShader.size(), &pipeline->layout), "Create RHI input layout");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = desc.cullBack ? D3D11_CULL_BACK : D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    raster.DepthBias = desc.depthBias; raster.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
    raster.DepthBiasClamp = desc.depthBias ? .01f : 0;
    Check(device_->CreateRasterizerState(&raster, &pipeline->raster), "Create RHI raster state");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = desc.depthTest;
    depth.DepthWriteMask = desc.depthTest ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    Check(device_->CreateDepthStencilState(&depth, &pipeline->depth), "Create RHI depth state");
    return pipeline;
}

std::unique_ptr<IRHISurface> D3D11Device::CreateSurface(void* nativeWindow, unsigned width, unsigned height) {
    if (!width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        throw std::invalid_argument("Invalid surface size");
    return std::make_unique<D3D11Surface>(device_.Get(),context_.Get(),nativeWindow,width,height);
}
}
