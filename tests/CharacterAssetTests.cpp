#include <remi/render/BakedAnimation.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
struct Header { char magic[4]; std::uint32_t version, vertices, indices, clips, reserved; float fps; };
struct Entry { char name[32]; std::uint32_t frames; };
static_assert(sizeof(Header) == 28 && sizeof(Entry) == 36);
void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, bool duplicate) {
    std::ofstream out(path,std::ios::binary);
    const Header header{{'R','M','C','H'},2,3,3,2,0,15};
    const std::array<remi::Vec3,3> colors{{{1,0,0},{0,1,0},{0,0,1}}};
    const std::array<std::uint32_t,3> indices{0,1,2};
    Entry entries[2] = {{"stand",2},{"sprint",2}};
    if (duplicate) entries[1] = entries[0];
    const std::array<remi::Vec3,12> positions{{
        {-1,0,0},{1,0,0},{0,1,0}, {-1,0,0},{1,0,0},{0,1.1f,0},
        {-1,0,0},{1,0,0},{0,1.5f,0}, {-1,0,0},{1,0,0},{0,1.6f,0}}};
    out.write(reinterpret_cast<const char*>(&header),sizeof(header));
    out.write(reinterpret_cast<const char*>(colors.data()),sizeof(colors));
    out.write(reinterpret_cast<const char*>(indices.data()),sizeof(indices));
    out.write(reinterpret_cast<const char*>(entries),sizeof(entries));
    out.write(reinterpret_cast<const char*>(positions.data()),sizeof(positions));
    Check(static_cast<bool>(out),"Could not write test asset");
}
}
int main() {
    try {
        const auto path = std::filesystem::temp_directory_path()/"remi-character-asset-test.rmc";
        Write(path,false);
        auto asset = remi::BakedAnimation::Load(path);
        Check(asset->HasClip("stand") && asset->HasClip("sprint") && !asset->HasClip("idle"),"Named clip lookup failed");
        Check(asset->ClipNames().size() == 2 && asset->ClipNames()[1] == "sprint","Clip directory order failed");
        Check(asset->IdleFrames() == 0 && asset->RunFrames() == 0,"Legacy names leaked into new asset");
        Write(path,true);
        bool rejected = false;
        try { (void)remi::BakedAnimation::Load(path); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected,"Duplicate clip names accepted");
        std::filesystem::remove(path);
        std::cout << "Named character asset and duplicate validation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
