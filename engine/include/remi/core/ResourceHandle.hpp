#pragma once
#include <cstdint>
namespace remi {
template<class T> struct ResourceHandle {
    std::uint64_t id = 0, owner = 0;
    friend bool operator==(const ResourceHandle&, const ResourceHandle&) = default;
};
class Mesh;
using MeshHandle = ResourceHandle<Mesh>;
}
