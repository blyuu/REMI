#pragma once
#include <remi/core/Math.hpp>
#include <remi/render/ShadingModel.hpp>

namespace remi {
struct MaterialProperties {
    Vec3 tint{1,1,1};
    float metallic = 0;
    float roughness = .6f;
    float emissive = 0;
    ShadingModel shadingModel = ShadingModel::Standard;
};
}
