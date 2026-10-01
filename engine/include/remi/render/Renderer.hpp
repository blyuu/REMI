#pragma once
#include <remi/render/Camera.hpp>
#include <remi/render/Material.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace remi {
struct Vertex { Vec3 position; Vec3 color; Vec3 normal; Vec2 uv; };
struct RendererConfig {
    void* nativeWindow = nullptr;
    unsigned width = 1280, height = 720;
    std::filesystem::path shaderFile;
    std::string shaderSource; // Optional loaded source; includes unsupported in this path.
    bool useWarp = false;
    bool requestDebug = true;
    bool requireDebug = false;
    bool vsync = true;
};
class Mesh {
public:
    [[nodiscard]] bool IntersectsClip(const Matrix4& modelViewProjection) const noexcept;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
private:
    friend class Renderer;
    struct Impl;
    Mesh();
    std::unique_ptr<Impl> impl_;
};
struct FrameImage { unsigned width = 0, height = 0; std::vector<std::uint8_t> rgba; };
struct ShutdownReport { bool debugValidated = false; unsigned liveChildren = 0; unsigned priorWarnings = 0; };
struct GpuTimings { bool valid = false; double shadowMs = 0, colorMs = 0, totalMs = 0; std::uint64_t sampleId = 0; };
struct DirectionalLight {
    Vec3 direction{-.5f,-1,.4f}; // Direction in which light travels.
    float intensity = 1;
    Vec3 color{1,.96f,.88f};
    float ambient = .18f;
    Vec3 cameraPosition{};
};
[[nodiscard]] Matrix4 DirectionalShadowMatrix(Vec3 center, Vec3 direction, float extent = 16);
class Renderer {
public:
    explicit Renderer(const RendererConfig& config);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    [[nodiscard]] std::unique_ptr<Mesh> CreateMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    [[nodiscard]] std::unique_ptr<Mesh> CreateDynamicMesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
    void UpdateMeshVertices(Mesh& mesh, std::span<const Vertex> vertices);
    void SetMeshTexture(Mesh& mesh, unsigned width, unsigned height, std::span<const std::uint8_t> rgba, bool srgb = true);
    void Resize(unsigned width, unsigned height);
    void BeginFrame(Vec3 clearColor = {.035f, .055f, .085f});
    void Draw(const Mesh& mesh, const Matrix4& modelViewProjection);
    void BeginShadow(const Matrix4& lightViewProjection);
    void EndShadow();
    void DrawLit(const Mesh& mesh, const Matrix4& world, const Matrix4& viewProjection, const DirectionalLight& light, const MaterialProperties& material = {});
    // Reuses the transform already computed for culling.
    void DrawLitPrepared(const Mesh& mesh, const Matrix4& world, const Matrix4& modelViewProjection, const DirectionalLight& light, const MaterialProperties& material = {});
    void Present();
    // Rebuilds shader objects and the input layout after a source change.
    // Compile failures leave the previous GPU shaders active.
    [[nodiscard]] bool ReloadShaders();
    [[nodiscard]] bool ReplaceShaderSource(std::string source);
    void BeginGpuProfile(); // Start before BeginShadow/BeginFrame.
    void EndGpuProfile(); // End after final color draw, before Present.
    [[nodiscard]] GpuTimings PollGpuProfile(); // Nonblocking; last completed sample or invalid.
    // Synchronous diagnostic readback. Call before Present; never in normal frame path.
    [[nodiscard]] FrameImage Readback();
    void SaveScreenshot(const std::filesystem::path& path);
    [[nodiscard]] bool DebugLayerEnabled() const noexcept;
    // Returns count of corruption/error/warning messages and logs each. Clears queue.
    [[nodiscard]] unsigned CheckDiagnostics();
    [[nodiscard]] std::size_t LiveMeshes() const noexcept;
    // Release Mesh owners first. Releases renderer resources, then audits D3D live objects.
    // Renderer cannot draw after this call. Destruction remains safe without explicit audit.
    [[nodiscard]] ShutdownReport ShutdownAndValidate();
    [[nodiscard]] unsigned Width() const noexcept;
    [[nodiscard]] unsigned Height() const noexcept;
private:
    [[nodiscard]] std::unique_ptr<Mesh> CreateMeshInternal(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices, bool dynamic);
    void DrawInternal(const Mesh& mesh, const Matrix4& mvp, const Matrix4& world, const DirectionalLight* light, const MaterialProperties& material);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
