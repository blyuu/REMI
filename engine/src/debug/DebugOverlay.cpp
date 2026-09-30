#include <remi/debug/DebugOverlay.hpp>
#include <array>
#include <stdexcept>
namespace remi::debug {
namespace {
using Glyph = std::array<unsigned char,7>;
Glyph Pattern(char c) {
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,14}; case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {31,4,4,4,4,4,31}; case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
    case '.': return {0,0,0,0,0,12,12}; case ':': return {0,12,12,0,12,12,0};
    case '/': return {1,1,2,4,8,16,16}; case '-': return {0,0,0,31,0,0,0};
    case '%': return {17,2,4,4,8,17,0}; case ' ': return {};
    default: return {31,1,2,4,4,0,4};
    }
}
}
void DebugOverlay::Update(Renderer& renderer,unsigned width,unsigned height,std::span<const std::string> lines) {
    if (width < 260 || height < 120 || lines.empty()) { mesh_.reset(); return; }
    std::vector<Vertex> vertices; std::vector<std::uint32_t> indices;
    const auto quad = [&](float x,float y,float w,float h,Vec3 color,float depth = .1f) {
        const auto start = static_cast<std::uint32_t>(vertices.size());
        const auto px = [&](float v) { return 2*v/static_cast<float>(width)-1; };
        const auto py = [&](float v) { return 1-2*v/static_cast<float>(height); };
        vertices.push_back({{px(x),py(y),depth},color}); vertices.push_back({{px(x+w),py(y),depth},color});
        vertices.push_back({{px(x),py(y+h),depth},color}); vertices.push_back({{px(x+w),py(y+h),depth},color});
        indices.insert(indices.end(),{start,start+1,start+2,start+2,start+1,start+3});
    };
    const float scale = 2, rowHeight = 20, panelWidth = static_cast<float>(width < 870 ? width-16 : 854);
    const float panelHeight = 16+rowHeight*static_cast<float>(lines.size());
    quad(8,8,panelWidth,panelHeight,{.012f,.024f,.04f});
    quad(8,8,4,panelHeight,{.02f,.65f,.78f},0);
    for (std::size_t row = 0; row < lines.size(); ++row) {
        float x = 20, y = 16+static_cast<float>(row)*rowHeight;
        const Vec3 color = row == 0 ? Vec3{.2f,.95f,.95f} : Vec3{.8f,.9f,.95f};
        for (char c : lines[row]) {
            if (x+12 >= panelWidth) break;
            const auto glyph = Pattern(c);
            for (unsigned gy = 0; gy < 7; ++gy) for (unsigned gx = 0; gx < 5; ++gx)
                if (glyph[gy] & (1u << (4-gx))) quad(x+gx*scale,y+gy*scale,scale,scale,color,0);
            x += 12;
        }
    }
    mesh_ = renderer.CreateMesh(vertices,indices);
}
void DebugOverlay::Draw(Renderer& renderer) const { if (mesh_) renderer.Draw(*mesh_,Matrix4::Identity()); }
}
