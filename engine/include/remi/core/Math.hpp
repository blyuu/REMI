#pragma once
#include <array>

namespace remi {
struct Vec3 { float x = 0, y = 0, z = 0; };
// Row-major storage, row vectors: local * parentWorld * view * projection.
struct Matrix4 {
    std::array<float, 16> values{};
    [[nodiscard]] static Matrix4 Identity() noexcept;
};
[[nodiscard]] Matrix4 Multiply(const Matrix4& a, const Matrix4& b) noexcept;
// Radians; S * Rx * Ry * Rz * T. No graphics API dependency.
[[nodiscard]] Matrix4 TransformMatrix(Vec3 position, Vec3 rotation, Vec3 scale) noexcept;
[[nodiscard]] Matrix4 ModelMatrix(Vec3 position, float yaw, Vec3 scale) noexcept;
}
