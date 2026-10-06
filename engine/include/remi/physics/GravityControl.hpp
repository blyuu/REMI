#pragma once
#include <remi/physics/PhysicsWorld.hpp>

namespace remi {
// Reverses the world's current gravity direction and removes only this body's
// velocity along that direction. The caller chooses whether support is required.
[[nodiscard]] bool TryReverseGravity(PhysicsWorld& world, EntityId body, bool requireSupport = true);
}
