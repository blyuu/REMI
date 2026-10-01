#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>

namespace remi::rhi {
enum class Backend { D3D11 };
enum class Driver { Hardware, Warp };

struct DeviceDesc {
    Backend backend = Backend::D3D11;
    Driver driver = Driver::Hardware;
    bool requestDebug = true;
    bool requireDebug = false;
};

struct DeviceInfo {
    Backend backend = Backend::D3D11;
    Driver driver = Driver::Hardware;
    bool debugLayer = false;
    std::string adapterName;
};

enum class BufferBind { Vertex, Index, Constant };
struct BufferDesc {
    std::size_t byteSize = 0;
    BufferBind bind = BufferBind::Vertex;
    bool dynamic = false;
    const void* initialData = nullptr;
};

enum class TextureFormat { RGBA8, RGBA8Srgb, Depth32 };
struct TextureDesc {
    unsigned width = 0, height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    bool renderTarget = false;
    const void* pixels = nullptr; // tightly packed RGBA8 for sampled color textures
};
struct RenderTargetDesc {
    unsigned width = 0, height = 0;
    bool depth = true;
};
struct TextureReadback {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
enum class VertexFormat { Float2, Float3, Float4 };
struct InputElement {
    std::string semantic;
    unsigned offset = 0;
    VertexFormat format = VertexFormat::Float3;
};
struct PipelineDesc {
    std::vector<std::uint8_t> vertexShader, pixelShader;
    std::vector<InputElement> inputs;
    bool depthTest = true;
    bool cullBack = true;
    int depthBias = 0;
    float slopeScaledDepthBias = 0;
};
}
