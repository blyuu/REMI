#pragma once
#include <remi/rhi/RHI.hpp>
#include <d3d11.h>
#include <wrl/client.h>

namespace remi::rhi {
class D3D11Buffer final : public IRHIBuffer {
public:
    D3D11Buffer(Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, std::size_t size, bool dynamic, ID3D11Device* owner);
    [[nodiscard]] std::size_t ByteSize() const noexcept override { return size_; }
    [[nodiscard]] bool Dynamic() const noexcept override { return dynamic_; }
    [[nodiscard]] ID3D11Buffer* NativeBuffer() const noexcept { return buffer_.Get(); }
    [[nodiscard]] ID3D11Device* Owner() const noexcept { return owner_; }
private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
    std::size_t size_;
    bool dynamic_;
    ID3D11Device* owner_;
};

// Temporary backend bridge for the existing D3D11 Renderer implementation.
// No D3D11 types appear in the public RHI interface.
class D3D11Device final : public IRHIDevice {
public:
    explicit D3D11Device(const DeviceDesc& desc);
    [[nodiscard]] const DeviceInfo& Info() const noexcept override { return info_; }
    [[nodiscard]] ID3D11Device* NativeDevice() const noexcept { return device_.Get(); }
    [[nodiscard]] ID3D11DeviceContext* NativeContext() const noexcept { return context_.Get(); }
    [[nodiscard]] std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc& desc) override;
    void UpdateBuffer(IRHIBuffer& buffer, const void* data, std::size_t bytes) override;
    [[nodiscard]] std::unique_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) override;
    [[nodiscard]] std::unique_ptr<IRHIRenderTarget> CreateRenderTarget(const RenderTargetDesc& desc) override;
    [[nodiscard]] std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) override;
    [[nodiscard]] std::unique_ptr<IRHISurface> CreateSurface(void* nativeWindow, unsigned width, unsigned height) override;
    [[nodiscard]] IRHIContext& Context() noexcept override { return *contextApi_; }
private:
    DeviceInfo info_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::unique_ptr<IRHIContext> contextApi_;
};

}
