#include <remi/render/BakedAnimation.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace remi {
namespace {
struct Header {
    char magic[4];
    std::uint32_t version, vertices, indices, idleFrames, runFrames;
    float fps;
};
static_assert(sizeof(Header) == 28 && sizeof(Vec3) == 12);
bool Finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
void ComputeNormals(std::span<Vertex> vertices, std::span<const std::uint32_t> indices) {
    for (auto& vertex : vertices) vertex.normal = {};
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto a = indices[i], b = indices[i+1], c = indices[i+2];
        const auto p = vertices[a].position, q = vertices[b].position, r = vertices[c].position;
        const Vec3 u{q.x-p.x,q.y-p.y,q.z-p.z}, v{r.x-p.x,r.y-p.y,r.z-p.z};
        const Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        for (auto index : {a,b,c}) {
            auto& result = vertices[index].normal;
            result.x += n.x; result.y += n.y; result.z += n.z;
        }
    }
    for (auto& vertex : vertices) {
        auto& n = vertex.normal;
        const float length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if (length > 1e-10f) { n.x /= length; n.y /= length; n.z /= length; }
        else n = {};
    }
}
}
std::unique_ptr<BakedAnimation> BakedAnimation::Load(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open baked character animation");
    Header header{};
    stream.read(reinterpret_cast<char*>(&header),sizeof(header));
    if (!stream || header.magic[0] != 'R' || header.magic[1] != 'M' || header.magic[2] != 'C' || header.magic[3] != 'H' ||
        header.version != 1 || !header.vertices || header.vertices > 100000 || !header.indices || header.indices > 600000 ||
        header.indices % 3 || !header.idleFrames || header.idleFrames > 1000 || !header.runFrames || header.runFrames > 1000 ||
        !std::isfinite(header.fps) || header.fps < 1 || header.fps > 120)
        throw std::runtime_error("Invalid baked character animation header");
    const auto vertexCount = static_cast<std::uint64_t>(header.vertices);
    const auto frames = static_cast<std::uint64_t>(header.idleFrames) + header.runFrames;
    const auto expected = sizeof(Header) + vertexCount*sizeof(Vec3) +
        static_cast<std::uint64_t>(header.indices)*sizeof(std::uint32_t) + frames*vertexCount*sizeof(Vec3);
    if (std::filesystem::file_size(path) != expected) throw std::runtime_error("Baked character animation size mismatch");
    auto result = std::unique_ptr<BakedAnimation>(new BakedAnimation());
    result->colors_.resize(header.vertices); result->indices_.resize(header.indices);
    result->positions_.resize(static_cast<std::size_t>(frames*vertexCount));
    stream.read(reinterpret_cast<char*>(result->colors_.data()),result->colors_.size()*sizeof(Vec3));
    stream.read(reinterpret_cast<char*>(result->indices_.data()),result->indices_.size()*sizeof(std::uint32_t));
    stream.read(reinterpret_cast<char*>(result->positions_.data()),result->positions_.size()*sizeof(Vec3));
    if (!stream) throw std::runtime_error("Baked character animation truncated");
    for (auto index : result->indices_) if (index >= header.vertices) throw std::runtime_error("Baked character index out of range");
    for (auto color : result->colors_) if (!Finite(color)) throw std::runtime_error("Baked character color is invalid");
    for (auto position : result->positions_) if (!Finite(position)) throw std::runtime_error("Baked character pose is invalid");
    result->normals_.resize(result->positions_.size());
    std::vector<Vertex> pose(header.vertices);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t start = frame*vertexCount;
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
            pose[vertex].position = result->positions_[start+vertex];
        ComputeNormals(pose,result->indices_);
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
            result->normals_[start+vertex] = pose[vertex].normal;
    }
    result->scratch_.resize(header.vertices);
    result->idleFrames_ = header.idleFrames; result->runFrames_ = header.runFrames; result->fps_ = header.fps;
    return result;
}
std::unique_ptr<Mesh> BakedAnimation::CreateMesh(Renderer& renderer) const {
    std::vector<Vertex> vertices(colors_.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i] = {positions_[i],colors_[i],normals_[i]};
    return renderer.CreateDynamicMesh(vertices,indices_);
}
void BakedAnimation::Update(Renderer& renderer, Mesh& mesh, bool running, double elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0) throw std::invalid_argument("Invalid animation elapsed time");
    if (running != running_) { running_ = running; phase_ = 0; }
    const std::size_t count = running ? runFrames_ : idleFrames_;
    const std::size_t offset = running ? idleFrames_ : 0;
    phase_ = std::fmod(phase_ + elapsedSeconds*fps_,static_cast<double>(count));
    const auto first = static_cast<std::size_t>(phase_);
    const auto second = (first+1) % count;
    const float blend = static_cast<float>(phase_ - first);
    const std::size_t stride = colors_.size();
    for (std::size_t vertex = 0; vertex < stride; ++vertex) {
        const auto a = positions_[(offset+first)*stride+vertex];
        const auto b = positions_[(offset+second)*stride+vertex];
        const auto na = normals_[(offset+first)*stride+vertex];
        const auto nb = normals_[(offset+second)*stride+vertex];
        const Vec3 n{na.x+(nb.x-na.x)*blend,na.y+(nb.y-na.y)*blend,na.z+(nb.z-na.z)*blend};
        const float length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        scratch_[vertex] = {{a.x+(b.x-a.x)*blend,a.y+(b.y-a.y)*blend,a.z+(b.z-a.z)*blend},colors_[vertex],
                            length > 1e-10f ? Vec3{n.x/length,n.y/length,n.z/length} : Vec3{}};
    }
    renderer.UpdateMeshVertices(mesh,scratch_);
}
}
