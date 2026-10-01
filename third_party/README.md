# Third-party headers

These headers were carried over from the previous local REMI project. Their original copyright and MIT license notices remain in each file.

| Header | Use |
| --- | --- |
| `cgltf/cgltf.h` | glTF/GLB parsing and accessor decoding |
| `stb/stb_image.h` | PNG/JPEG image decoding |
| `json/json.hpp` | `.remimat` JSON serialization |

The engine builds each single-header implementation in one dedicated translation unit (`CgltfImpl.cpp` and `StbImpl.cpp`).
