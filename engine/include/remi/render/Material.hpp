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
    Vec3 emissiveFactor{};
    float alphaCutoff = 0; // Zero keeps the opaque path; positive values discard masked pixels.
};
}
