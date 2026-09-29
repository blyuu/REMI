#include <remi/physics/PhysicsWorld.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
bool Near(float a,float b,float tolerance = .001f) { return std::abs(a-b) < tolerance; }
int main() {
    try {
        {
            remi::Scene freeScene; remi::PhysicsWorld freeWorld(freeScene);
            const auto id = freeScene.Create(); freeWorld.AddBody(id,{remi::BodyType::Dynamic});
            for (unsigned i = 0; i < 60; ++i) freeWorld.Step(1.f/60);
            Check(Near(freeWorld.Velocity(id).y,-9.81f) && Near(freeScene.Get<remi::TransformComponent>(id)->position.y,-4.98675f),"Semi-implicit fixed-step integration failed");
        }
        remi::Scene scene; remi::PhysicsWorld physics(scene);
        const auto box = [&](remi::Vec3 p,remi::Vec3 extent,remi::BodyType type) {
            const auto id = scene.Create(); scene.Get<remi::TransformComponent>(id)->position = p;
            physics.AddBody(id,{type,extent}); return id;
        };
        const auto floor = box({0,-.5f,0},{10,.5f,10},remi::BodyType::Static);
        const auto body = box({0,3,0},{.5f,.5f,.5f},remi::BodyType::Dynamic);
        for (unsigned i = 0; i < 300; ++i) physics.Step(1.f/60);
        Check(Near(scene.Get<remi::TransformComponent>(body)->position.y,.5f) && Near(physics.Velocity(body).y,0) && physics.Supported(body),"Floor settling failed");
        const auto ceiling = box({0,5.5f,0},{10,.5f,10},remi::BodyType::Static);
        physics.SetGravity({0,9.81f,0});
        Check(!physics.Supported(body),"Gravity change retained stale support");
        for (unsigned i = 0; i < 300; ++i) physics.Step(1.f/60);
        Check(Near(scene.Get<remi::TransformComponent>(body)->position.y,4.5f) && physics.Supported(body),"Inverted gravity ceiling failed");
        (void)floor; (void)ceiling;
        const auto wall = box({3,2,0},{.1f,2,10},remi::BodyType::Static);
        physics.SetGravity({}); scene.Get<remi::TransformComponent>(body)->position = {0,2,0};
        physics.SetVelocity(body,{1000,0,0}); physics.Step(1.f/60);
        Check(Near(scene.Get<remi::TransformComponent>(body)->position.x,2.4f) && Near(physics.Velocity(body).x,0),"Fast wall sweep failed");
        Check(!physics.Supported(body),"Zero gravity reported support");
        physics.SetGravity({9.81f,0,0}); physics.Step(1.f/60);
        Check(physics.Supported(body),"Sideways gravity support failed");
        physics.SetGravity({});
        scene.Get<remi::TransformComponent>(body)->position = {2.8f,2,0}; physics.SetVelocity(body,{}); physics.Step(1.f/60);
        Check(Near(scene.Get<remi::TransformComponent>(body)->position.x,2.4f),"Initial overlap correction failed");
        const auto other = box({-1.8f,2,0},{.5f,.5f,.5f},remi::BodyType::Dynamic);
        scene.Get<remi::TransformComponent>(body)->position = {-1,2,0}; physics.SetVelocity(other,{2,0,0});
        physics.Step(1.f/60);
        Check(Near(physics.Velocity(other).x,1) && Near(physics.Velocity(body).x,1),"Equal mass inelastic response failed");
        bool rejected = false; try { physics.Step(0); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected,"Zero dt accepted");
        rejected = false; try { physics.SetGravity({0,std::numeric_limits<float>::quiet_NaN(),0}); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected,"NaN accepted");
        rejected = false; try { physics.AddBody(body,{}); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected,"Duplicate body accepted");
        scene.Destroy(other); physics.Step(1.f/60);
        Check(physics.Stats().bodies == 4,"Deleted entity body retained");
        const auto reused = scene.Create(); Check(!physics.RemoveBody(other) && !physics.Supported(reused),"Stale entity body revived");
        scene.SetParent(body,wall); rejected = false;
        try { physics.Step(1.f/60); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected,"Parented physics body silently simulated");
        scene.SetParent(body,{}); scene.Clear(); physics.Step(1.f/60);
        Check(physics.Stats().bodies == 0 && physics.Contacts().empty(),"Scene clear left bodies");
        std::cout << "300-tick floor/ceiling settling, sideways gravity, 1000 units/s wall sweep, overlap, momentum and stale IDs passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
