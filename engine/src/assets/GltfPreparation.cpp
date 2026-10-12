#include <remi/assets/GltfPreparation.hpp>
#include <algorithm>
#include <stdexcept>

namespace remi::assets {
std::vector<PreparedGltfPrimitive> PrepareGltfPrimitives(
    const std::vector<const gltf::Primitive*>& sources, JobSystem* jobs, std::size_t grainOverride) {
    std::vector<PreparedGltfPrimitive> prepared(sources.size());
    const auto prepare = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            const auto* source = sources[i];
            if (!source) throw std::invalid_argument("Null glTF primitive");
            if (source->vertices.empty() || source->indices.empty()) continue;
            auto& output = prepared[i]; // Each range writes separate elements.
            output.vertices.reserve(source->vertices.size());
            for (const auto& vertex : source->vertices)
                output.vertices.push_back({{vertex.position.x,vertex.position.y,-vertex.position.z},
                    {vertex.color.x,vertex.color.y,vertex.color.z},
                    {vertex.normal.x,vertex.normal.y,-vertex.normal.z},
                    {vertex.uv0.x,vertex.uv0.y}});
            output.indices = source->indices;
            for (std::size_t triangle = 0; triangle + 2 < output.indices.size(); triangle += 3)
                std::swap(output.indices[triangle+1],output.indices[triangle+2]);
        }
    };
    if (jobs && prepared.size() > 1) {
        const auto grain = grainOverride ? grainOverride :
            std::max<std::size_t>(1,prepared.size()/(jobs->WorkerCount()*4));
        jobs->ParallelFor(prepared.size(),grain,prepare);
    } else prepare(0,prepared.size());
    return prepared;
}
}
