#pragma once
#include <remi/assets/GltfLoader.hpp>
#include <remi/core/JobSystem.hpp>
#include <remi/render/Renderer.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace remi::assets {
struct PreparedGltfPrimitive {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// The CPU conversion used by GltfAsset before any D3D resource creation.
// Repeated primitive pointers let the benchmark vary workload without changing input data.
[[nodiscard]] std::vector<PreparedGltfPrimitive> PrepareGltfPrimitives(
    const std::vector<const gltf::Primitive*>& sources, JobSystem* jobs = nullptr,
    std::size_t grainOverride = 0);
}
