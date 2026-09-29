#include <remi/render/Camera.hpp>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace remi {
namespace {
Matrix4 Store(DirectX::FXMMATRIX matrix) noexcept {
    DirectX::XMFLOAT4X4 value;
    DirectX::XMStoreFloat4x4(&value, matrix);
    Matrix4 result;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col) result.values[row * 4 + col] = value.m[row][col];
    return result;
}
}
void Camera::Orbit(float dx, float dy) noexcept {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    yaw = std::remainder(yaw + dx * .005f, DirectX::XM_2PI);
    pitch = std::clamp(pitch + dy * .005f, -.9f, 1.4f);
}
void Camera::Zoom(float wheel) noexcept {
    if (std::isfinite(wheel)) distance = std::clamp(distance - wheel * .5f, 1.f, 30.f);
}
Vec3 Camera::Position() const noexcept {
    return {target.x + std::sin(yaw) * std::cos(pitch) * distance,
            target.y + std::sin(pitch) * distance,
            target.z - std::cos(yaw) * std::cos(pitch) * distance};
}
Matrix4 Camera::ViewProjection(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0 || !std::isfinite(verticalFov) || verticalFov <= 0 ||
        verticalFov >= DirectX::XM_PI || !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0 || farPlane <= nearPlane || !std::isfinite(distance) || distance <= 0 ||
        !std::isfinite(yaw) || !std::isfinite(pitch) || std::abs(pitch) >= DirectX::XM_PIDIV2 ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
        throw std::invalid_argument("Invalid camera projection or pose");
    const auto eye = Position();
    return Store(DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(eye.x, eye.y, eye.z, 1),
                 DirectX::XMVectorSet(target.x, target.y, target.z, 1), DirectX::XMVectorSet(0, 1, 0, 0)) *
                 DirectX::XMMatrixPerspectiveFovLH(verticalFov, aspect, nearPlane, farPlane));
}
}
