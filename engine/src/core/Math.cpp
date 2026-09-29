#include <remi/core/Math.hpp>
#include <cmath>

namespace remi {
Matrix4 Matrix4::Identity() noexcept {
    Matrix4 result;
    result.values[0] = result.values[5] = result.values[10] = result.values[15] = 1;
    return result;
}
Matrix4 Multiply(const Matrix4& a, const Matrix4& b) noexcept {
    Matrix4 result;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            for (unsigned k = 0; k < 4; ++k)
                result.values[row * 4 + col] += a.values[row * 4 + k] * b.values[k * 4 + col];
    return result;
}
Matrix4 TransformMatrix(Vec3 position, Vec3 rotation, Vec3 scale) noexcept {
    auto s = Matrix4::Identity(), x = s, y = s, z = s, t = s;
    s.values[0] = scale.x; s.values[5] = scale.y; s.values[10] = scale.z;
    const float cx = std::cos(rotation.x), sx = std::sin(rotation.x);
    const float cy = std::cos(rotation.y), sy = std::sin(rotation.y);
    const float cz = std::cos(rotation.z), sz = std::sin(rotation.z);
    x.values[5] = cx; x.values[6] = sx; x.values[9] = -sx; x.values[10] = cx;
    y.values[0] = cy; y.values[2] = -sy; y.values[8] = sy; y.values[10] = cy;
    z.values[0] = cz; z.values[1] = sz; z.values[4] = -sz; z.values[5] = cz;
    t.values[12] = position.x; t.values[13] = position.y; t.values[14] = position.z;
    return Multiply(Multiply(Multiply(Multiply(s,x),y),z),t);
}
Matrix4 ModelMatrix(Vec3 position, float yaw, Vec3 scale) noexcept {
    return TransformMatrix(position, {0,yaw,0}, scale);
}
}
