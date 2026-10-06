#include <remi/render/PrimitiveMesh.hpp>
#include <array>

namespace remi {
std::unique_ptr<Mesh> CreateBoxMesh(Renderer& renderer, Vec3 color) {
    std::array<Vertex,8> vertices{};
    for (unsigned i = 0; i < 8; ++i)
        vertices[i] = {{(i&1) ? .5f:-.5f,(i&2) ? .5f:-.5f,(i&4) ? .5f:-.5f},color};
    const std::array<std::uint32_t,36> indices{
        0,2,1,1,2,3,5,7,4,4,7,6,4,6,0,0,6,2,
        1,3,5,5,3,7,2,6,3,3,6,7,4,0,5,5,0,1};
    return renderer.CreateMesh(vertices,indices);
}
}
