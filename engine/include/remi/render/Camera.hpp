#pragma once
#include <remi/core/Math.hpp>

namespace remi {
class Camera {
public:
    Vec3 target{0, .5f, 0};
    float yaw = -.55f, pitch = .35f, distance = 7;
    float verticalFov = 0.785398163f;
    float nearPlane = .05f, farPlane = 100;
    void Orbit(float dx, float dy) noexcept;
    void Zoom(float wheel) noexcept;
    [[nodiscard]] Vec3 Position() const noexcept;
    [[nodiscard]] Matrix4 ViewProjection(float aspect) const;
};
}
