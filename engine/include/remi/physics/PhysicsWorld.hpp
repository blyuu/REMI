#pragma once
#include <remi/scene/Scene.hpp>
#include <vector>
#include <cstdint>
namespace remi {
enum class BodyType { Static, Dynamic };
struct PhysicsSettings {
    Vec3 gravity{0,-9.81f,0};
    float maxStepSeconds = 1.f/30;
    float contactTolerance = .0001f;
    unsigned solverIterations = 8;
    float restitution = 0;
};
struct BodyDesc {
    BodyType type = BodyType::Static;
    Vec3 halfExtent{.5f,.5f,.5f}; // World-space axis-aligned extent, independent of visual scale.
    Vec3 velocity{};
    float mass = 1;
    std::uint32_t collisionLayer = 1;
    std::uint32_t collisionMask = ~0u;
};
struct Contact { EntityId a, b; Vec3 normal; }; // Normal points from b toward a.
struct PhysicsStats { std::size_t bodies = 0, candidates = 0, contacts = 0; };
// Main-thread, Scene-bound world. Scene must outlive this object.
class PhysicsWorld {
public:
    explicit PhysicsWorld(Scene& scene, PhysicsSettings settings = {});
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void AddBody(EntityId entity, const BodyDesc& desc);
    bool RemoveBody(EntityId entity);
    void SetGravity(Vec3 gravity);
    [[nodiscard]] Vec3 Gravity() const noexcept { return gravity_; }
    [[nodiscard]] const PhysicsSettings& Settings() const noexcept { return settings_; }
    void SetVelocity(EntityId entity, Vec3 velocity);
    [[nodiscard]] Vec3 Velocity(EntityId entity) const;
    [[nodiscard]] bool Supported(EntityId entity) const noexcept;
    void Step(float seconds); // Fixed dt (0, 1/30], caller owns accumulator.
    [[nodiscard]] const std::vector<Contact>& Contacts() const noexcept { return contacts_; }
    [[nodiscard]] PhysicsStats Stats() const noexcept { return stats_; }
private:
    struct Body { EntityId entity; BodyDesc desc; };
    void ValidateEntity(EntityId entity) const;
    Scene& scene_;
    PhysicsSettings settings_;
    Vec3 gravity_{0,-9.81f,0};
    std::vector<Body> bodies_;
    std::vector<Contact> contacts_;
    PhysicsStats stats_;
};
}
