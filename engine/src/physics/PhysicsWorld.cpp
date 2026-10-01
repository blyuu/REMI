#include <remi/physics/PhysicsWorld.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace remi {
namespace {
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float& Axis(Vec3& v,unsigned a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
float Axis(const Vec3& v,unsigned a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
bool Collides(const BodyDesc& a, const BodyDesc& b) {
    return (a.collisionMask & b.collisionLayer) && (b.collisionMask & a.collisionLayer);
}
}
PhysicsWorld::PhysicsWorld(Scene& scene, PhysicsSettings settings) : scene_(scene), settings_(settings), gravity_(settings.gravity) {
    if (!Finite(settings.gravity) || !std::isfinite(settings.maxStepSeconds) || settings.maxStepSeconds <= 0 ||
        settings.maxStepSeconds > 1 || !std::isfinite(settings.contactTolerance) || settings.contactTolerance < 0 ||
        settings.contactTolerance > .1f || settings.solverIterations == 0 || settings.solverIterations > 64 ||
        !std::isfinite(settings.restitution) || settings.restitution < 0 || settings.restitution > 1)
        throw std::invalid_argument("Invalid physics settings");
}
void PhysicsWorld::ValidateEntity(EntityId id) const {
    const auto* tr = scene_.Get<TransformComponent>(id);
    if (!tr || scene_.Parent(id) != EntityId{} || !Finite(tr->position) || tr->rotation.x != 0 || tr->rotation.y != 0 || tr->rotation.z != 0)
        throw std::invalid_argument("Physics body requires live root Transform with finite position and zero rotation");
}
void PhysicsWorld::AddBody(EntityId id,const BodyDesc& desc) {
    ValidateEntity(id);
    if (!Finite(desc.halfExtent) || desc.halfExtent.x <= 0 || desc.halfExtent.y <= 0 || desc.halfExtent.z <= 0 ||
        !Finite(desc.velocity) || !std::isfinite(desc.mass) || desc.mass < .000001f || desc.mass > 1000000 ||
        desc.collisionLayer == 0 ||
        (desc.type == BodyType::Static && Dot(desc.velocity,desc.velocity) != 0) ||
        (desc.type != BodyType::Static && desc.type != BodyType::Dynamic)) throw std::invalid_argument("Invalid body description");
    for (const auto& body : bodies_) if (body.entity == id) throw std::logic_error("Duplicate physics body");
    bodies_.push_back({id,desc});
}
bool PhysicsWorld::RemoveBody(EntityId id) {
    const auto removed = std::erase_if(bodies_,[&](const Body& body) { return body.entity == id; });
    std::erase_if(contacts_,[&](const Contact& c) { return c.a == id || c.b == id; });
    stats_.bodies = bodies_.size(); stats_.contacts = contacts_.size(); return removed != 0;
}
void PhysicsWorld::SetGravity(Vec3 gravity) {
    if (!Finite(gravity)) throw std::invalid_argument("Invalid gravity");
    gravity_ = gravity; contacts_.clear(); stats_.contacts = 0;
}
void PhysicsWorld::SetVelocity(EntityId id,Vec3 velocity) {
    if (!Finite(velocity)) throw std::invalid_argument("Invalid velocity");
    ValidateEntity(id);
    for (auto& body : bodies_) if (body.entity == id) {
        if (body.desc.type != BodyType::Dynamic) throw std::logic_error("Static velocity is unsupported");
        body.desc.velocity = velocity; return;
    }
    throw std::invalid_argument("Body not found");
}
Vec3 PhysicsWorld::Velocity(EntityId id) const {
    if (scene_.Alive(id)) for (const auto& body : bodies_) if (body.entity == id) return body.desc.velocity;
    throw std::invalid_argument("Body not found");
}
bool PhysicsWorld::Supported(EntityId id) const noexcept {
    if (!scene_.Alive(id)) return false;
    const float length = std::sqrt(Dot(gravity_,gravity_));
    if (length < .0001f) return false;
    for (const auto& c : contacts_) if (scene_.Alive(c.a) && scene_.Alive(c.b)) {
        if (c.a == id && Dot(c.normal,gravity_) < -.5f*length) return true;
        if (c.b == id && Dot(c.normal,gravity_) > .5f*length) return true;
    }
    return false;
}
void PhysicsWorld::Step(float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > settings_.maxStepSeconds) throw std::invalid_argument("Invalid physics step");
    std::erase_if(bodies_,[&](const Body& body) { return !scene_.Alive(body.entity); });
    for (const auto& body : bodies_) ValidateEntity(body.entity);
    // Validate integrated values before mutating any body.
    for (const auto& body : bodies_) if (body.desc.type == BodyType::Dynamic) {
        const auto p = scene_.Get<TransformComponent>(body.entity)->position; const auto v = body.desc.velocity;
        const Vec3 next{v.x+gravity_.x*dt,v.y+gravity_.y*dt,v.z+gravity_.z*dt};
        if (!Finite(next) || !Finite({p.x+next.x*dt,p.y+next.y*dt,p.z+next.z*dt})) throw std::overflow_error("Physics integration overflow");
    }
    contacts_.clear(); stats_ = {bodies_.size(),0,0};
    // Axis-separated sweeps against static AABBs prevent straight-line wall tunneling.
    for (auto& body : bodies_) if (body.desc.type == BodyType::Dynamic) {
        auto& p = scene_.Get<TransformComponent>(body.entity)->position;
        auto& v = body.desc.velocity;
        v.x += gravity_.x*dt; v.y += gravity_.y*dt; v.z += gravity_.z*dt;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const float start = Axis(p,axis), delta = Axis(v,axis)*dt;
            float end = start+delta;
            for (const auto& wall : bodies_) if (wall.desc.type == BodyType::Static && Collides(body.desc,wall.desc)) {
                ++stats_.candidates;
                const auto wp = scene_.Get<TransformComponent>(wall.entity)->position;
                bool transverse = true;
                for (unsigned other = 0; other < 3; ++other) if (other != axis &&
                    std::abs(Axis(p,other)-Axis(wp,other)) >= Axis(body.desc.halfExtent,other)+Axis(wall.desc.halfExtent,other)-settings_.contactTolerance*.1f) transverse = false;
                if (!transverse) continue;
                const float radius = Axis(body.desc.halfExtent,axis)+Axis(wall.desc.halfExtent,axis);
                const float low = Axis(wp,axis)-radius, high = Axis(wp,axis)+radius;
                if (delta > 0 && start <= low && end >= low) end = std::min(end,low);
                if (delta < 0 && start >= high && end <= high) end = std::max(end,high);
            }
            Axis(p,axis) = end;
            if (end != start+delta) Axis(v,axis) = -Axis(v,axis)*settings_.restitution;
        }
    }
    // Iterative mass-weighted penetration correction; perfectly inelastic normal response.
    for (unsigned iteration = 0; iteration < settings_.solverIterations; ++iteration) {
        for (std::size_t i = 0; i < bodies_.size(); ++i) for (std::size_t j = i+1; j < bodies_.size(); ++j) {
            auto& a = bodies_[i]; auto& b = bodies_[j];
            if (!Collides(a.desc,b.desc)) continue;
            const float ia = a.desc.type == BodyType::Dynamic ? 1/a.desc.mass : 0;
            const float ib = b.desc.type == BodyType::Dynamic ? 1/b.desc.mass : 0;
            if (ia+ib == 0) continue;
            ++stats_.candidates;
            auto& pa = scene_.Get<TransformComponent>(a.entity)->position;
            auto& pb = scene_.Get<TransformComponent>(b.entity)->position;
            float depth = 1e30f; unsigned axis = 0; bool overlap = true;
            for (unsigned k = 0; k < 3; ++k) {
                const float penetration = Axis(a.desc.halfExtent,k)+Axis(b.desc.halfExtent,k)-std::abs(Axis(pa,k)-Axis(pb,k));
                if (penetration < -settings_.contactTolerance) overlap = false;
                if (penetration < depth) { depth = penetration; axis = k; }
            }
            if (!overlap) continue;
            const float sign = Axis(pa,axis) >= Axis(pb,axis) ? 1.f : -1.f;
            const float correction = std::max(depth,0.f)/(ia+ib);
            Axis(pa,axis) += sign*correction*ia; Axis(pb,axis) -= sign*correction*ib;
            const float relative = (Axis(a.desc.velocity,axis)-Axis(b.desc.velocity,axis))*sign;
            if (relative < 0) {
                const float impulse = -(1.f+settings_.restitution)*relative/(ia+ib);
                Axis(a.desc.velocity,axis) += sign*impulse*ia; Axis(b.desc.velocity,axis) -= sign*impulse*ib;
            }
            if (iteration + 1 == settings_.solverIterations) { Vec3 normal{}; Axis(normal,axis) = sign; contacts_.push_back({a.entity,b.entity,normal}); }
        }
    }
    stats_.contacts = contacts_.size();
}
}
