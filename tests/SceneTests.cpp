#include <remi/scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(float value, float expected) { Check(std::abs(value - expected) < 1e-4f, "Transform mismatch"); }
int main() {
    try {
        remi::Scene scene, other;
        const auto first = scene.Create("cube");
        const auto foreign = other.Create("foreign");
        Check(scene.Size() == 1 && scene.Name(first) == "cube", "Create/name failed");
        scene.SetName(first,"renamed"); Check(scene.Name(first) == "renamed", "Rename failed");
        Check(!scene.Alive(foreign) && !other.Alive(first), "Cross-scene alias");
        Check(!scene.Alive({}) && !scene.Get<remi::TransformComponent>({}), "Null entity accepted");
        Check(scene.Get<remi::TransformComponent>(first) && !scene.Get<remi::MeshComponent>(first), "Default component state wrong");
        scene.Add<remi::MeshComponent>(first,{{7,1},true});
        bool rejected = false;
        try { scene.Add<remi::MeshComponent>(first); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected && scene.Get<remi::MeshComponent>(first)->mesh == remi::MeshHandle{7,1}, "Duplicate add modified component");
        Check(scene.Remove<remi::MeshComponent>(first) && !scene.Remove<remi::MeshComponent>(first), "Remove failed");
        for (int i = 0; i < 2048; ++i) (void)scene.Create();
        Check(scene.Alive(first) && scene.Name(first) == "renamed", "Vector growth invalidated ID");
        Check(scene.Destroy(first) && !scene.Destroy(first), "Destroy is not safe for stale ID");
        const auto reused = scene.Create();
        Check(reused.index == first.index && reused.generation != first.generation && !scene.Alive(first), "Generation reuse failed");
        Check(!scene.Get<remi::TransformComponent>(first) && !scene.Remove<remi::TransformComponent>(first), "Stale component access");
        rejected = false;
        try { scene.Add<remi::MeshComponent>(first); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Stale add accepted");
        const auto beforeClear = scene.Entities(); scene.Clear();
        Check(scene.Size() == 0 && scene.Entities().empty(), "Clear failed");
        for (auto id : beforeClear) Check(!scene.Alive(id), "Clear resurrected an ID");
        for (std::size_t i = 0; i < beforeClear.size(); ++i) (void)scene.Create();
        for (auto id : beforeClear) Check(!scene.Alive(id), "Clear/recreate resurrected an ID");
        scene.Clear();

        const auto root = scene.Create("root"), child = scene.Create("child"), grandchild = scene.Create("grandchild");
        auto* rootTr = scene.Get<remi::TransformComponent>(root);
        rootTr->position = {10,0,0}; rootTr->rotation.z = 1.57079632679f; rootTr->scale = {2,2,2};
        scene.Get<remi::TransformComponent>(child)->position = {1,0,0};
        scene.Get<remi::TransformComponent>(grandchild)->position = {0,1,0};
        Check(scene.SetParent(child,root) && scene.SetParent(grandchild,child), "Parent assignment failed");
        Check(!scene.SetParent(root,grandchild) && !scene.SetParent(root,root), "Hierarchy cycle accepted");
        Check(!scene.SetParent(child,foreign) && scene.Parent(child) == root, "Rejected parent mutated hierarchy");
        const auto world = scene.WorldMatrix(grandchild);
        Near(world.values[12],8); Near(world.values[13],2); Near(world.values[14],0);
        Check(scene.Children(root).size() == 1 && scene.Children(root)[0] == child, "Children query failed");
        Check(scene.SetParent(child,{}), "Detach failed");
        Near(scene.WorldMatrix(grandchild).values[12],1); Near(scene.WorldMatrix(grandchild).values[13],1);
        Check(scene.Remove<remi::TransformComponent>(child), "Transform removal failed");
        Near(scene.WorldMatrix(grandchild).values[12],0); Near(scene.WorldMatrix(grandchild).values[13],1);
        Check(scene.SetParent(child,root), "Reparent failed");
        Check(scene.Destroy(root) && !scene.Alive(child) && !scene.Alive(grandchild), "Subtree deletion left children");
        Check(scene.Size() == 0, "Subtree size wrong");

        auto ancestor = scene.Create(); const auto top = ancestor;
        for (int i = 0; i < 256; ++i) {
            const auto id = scene.Create(); scene.Get<remi::TransformComponent>(id)->position.x = 1;
            Check(scene.SetParent(id,ancestor), "Deep hierarchy assignment failed"); ancestor = id;
        }
        Near(scene.WorldMatrix(ancestor).values[12],256);
        Check(!scene.SetParent(top,ancestor), "Deep cycle accepted");
        Check(scene.Destroy(top) && scene.Size() == 0, "Deep subtree deletion failed");
        const auto invalid = scene.Create();
        scene.Get<remi::TransformComponent>(invalid)->scale.x = std::numeric_limits<float>::quiet_NaN();
        rejected = false;
        try { (void)scene.WorldMatrix(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "Non-finite transform accepted");
        std::cout << "Scene IDs/components/hierarchy/reuse regression passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
