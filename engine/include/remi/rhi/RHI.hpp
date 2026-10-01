#pragma once
#include <remi/rhi/RHITypes.hpp>
#include <memory>

namespace remi::rhi {
class IRHIBuffer {
public:
    virtual ~IRHIBuffer() = default;
    IRHIBuffer(const IRHIBuffer&) = delete;
    IRHIBuffer& operator=(const IRHIBuffer&) = delete;
    [[nodiscard]] virtual std::size_t ByteSize() const noexcept = 0;
    [[nodiscard]] virtual bool Dynamic() const noexcept = 0;
protected:
    IRHIBuffer() = default;
};

class IRHITexture {
public:
    virtual ~IRHITexture() = default;
    [[nodiscard]] virtual unsigned Width() const noexcept = 0;
    [[nodiscard]] virtual unsigned Height() const noexcept = 0;
};
class IRHIRenderTarget {
public:
    virtual ~IRHIRenderTarget() = default;
    [[nodiscard]] virtual unsigned Width() const noexcept = 0;
    [[nodiscard]] virtual unsigned Height() const noexcept = 0;
    [[nodiscard]] virtual const IRHITexture& Color() const noexcept = 0;
};
class IRHIPipeline { public: virtual ~IRHIPipeline() = default; };
class IRHIContext {
public:
    virtual ~IRHIContext() = default;
    // Used when a platform swap chain or legacy shadow target is bound externally.
    virtual void BeginExternalPass() = 0;
    virtual void EndExternalPass() = 0;
    virtual void BeginPass(IRHIRenderTarget& target, const float clear[4]) = 0;
    virtual void EndPass() = 0;
    virtual void SetPipeline(const IRHIPipeline& pipeline) = 0;
    virtual void SetVertexBuffer(const IRHIBuffer& buffer, unsigned stride) = 0;
    virtual void SetIndexBuffer(const IRHIBuffer& buffer) = 0;
    virtual void SetConstantBuffer(unsigned slot, const IRHIBuffer& buffer) = 0;
    virtual void SetTexture(unsigned slot, const IRHITexture* texture) = 0;
    virtual void DrawIndexed(unsigned count) = 0;
    [[nodiscard]] virtual TextureReadback Readback(const IRHIRenderTarget& target) = 0;
};

// Swap-chain presentation and diagnostics remain platform/renderer concerns.
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;
    IRHIDevice(const IRHIDevice&) = delete;
    IRHIDevice& operator=(const IRHIDevice&) = delete;
    [[nodiscard]] virtual const DeviceInfo& Info() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual void UpdateBuffer(IRHIBuffer& buffer, const void* data, std::size_t bytes) = 0;
    [[nodiscard]] virtual std::unique_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<IRHIRenderTarget> CreateRenderTarget(const RenderTargetDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<IRHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;
    [[nodiscard]] virtual IRHIContext& Context() noexcept = 0;
protected:
    IRHIDevice() = default;
};
}
