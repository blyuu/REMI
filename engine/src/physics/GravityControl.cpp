#include <remi/physics/GravityControl.hpp>
#include <cmath>

namespace remi {
bool TryReverseGravity(PhysicsWorld& world, EntityId body, bool requireSupport) {
    if (requireSupport && !world.Supported(body)) return false;
    const auto gravity = world.Gravity();
    const double length = std::hypot(static_cast<double>(gravity.x),
        static_cast<double>(gravity.y),static_cast<double>(gravity.z));
    if (length < .0001) return false;
    const auto velocity = world.Velocity(body);
    const double nx = gravity.x/length, ny = gravity.y/length, nz = gravity.z/length;
    const double along = velocity.x*nx + velocity.y*ny + velocity.z*nz;
    const Vec3 tangent{static_cast<float>(velocity.x-along*nx),
        static_cast<float>(velocity.y-along*ny),static_cast<float>(velocity.z-along*nz)};
    world.SetVelocity(body,tangent);
    world.SetGravity({-gravity.x,-gravity.y,-gravity.z});
    return true;
}
}
