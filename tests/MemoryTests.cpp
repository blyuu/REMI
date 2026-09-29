#include <remi/scene/Scene.hpp>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#if defined(_DEBUG) && !defined(REMI_ASAN)
#include <crtdbg.h>
#endif

#ifndef REMI_ASAN
namespace { bool rejectAllocations = false; std::size_t rejectMinimum = 0; }
// Test-only fault injection. Production allocator and sanitizer allocator are unchanged.
void* operator new(std::size_t size) {
    if (rejectAllocations && size >= rejectMinimum) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
struct DenyAllocation {
    explicit DenyAllocation(std::size_t minimum = 0) { rejectMinimum = minimum; rejectAllocations = true; }
    ~DenyAllocation() { rejectAllocations = false; }
};
#endif
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void SceneWorkload() {
    remi::Scene scene;
    std::vector<remi::EntityId> ids; ids.reserve(128);
    std::size_t capacity = 0;
    for (unsigned round = 0; round < 100; ++round) {
        ids.clear();
        for (unsigned i = 0; i < 128; ++i) {
            const auto id = scene.Create(std::string(128, 'x'));
            scene.Add<remi::MeshComponent>(id,{{i,1},true}); ids.push_back(id);
        }
        const auto memory = scene.Memory();
        Check(memory.entities == 128 && memory.components == 256 && memory.retired == 0, "Ownership counts incorrect");
        if (round == 0) capacity = memory.capacity;
        Check(memory.capacity == capacity, "Repeated cycles grew slot capacity");
        scene.Clear();
        Check(scene.Memory().reusable == 128 && scene.Memory().components == 0, "Clear did not release components");
        for (auto id : ids) Check(!scene.Alive(id), "Cleared ID revived");
    }
    const auto old = scene.Create();
    scene.Reset();
    const auto fresh = scene.Create();
    Check(!scene.Alive(old) && old.scene != fresh.scene, "Reset resurrected ID");
    scene.Reset();
    Check(scene.Memory().slotCapacityBytes == 0 && scene.Memory().capacity == 0 && scene.Size() == 0, "Reset retained slot storage");
}
int main() {
    try {
        const auto baseline = remi::LifetimeToken::Snapshot();
        SceneWorkload(); // Warm up runtime/CRT before measuring retained heap blocks.
#if defined(_DEBUG) && !defined(REMI_ASAN)
        _CrtMemState before{}, after{}, difference{};
        _CrtMemCheckpoint(&before);
#endif
        SceneWorkload();
#if defined(_DEBUG) && !defined(REMI_ASAN)
        _CrtMemCheckpoint(&after);
        Check(_CrtMemDifference(&difference,&before,&after) == 0, "CRT heap grew after complete scene destruction");
#endif
        Check(remi::LifetimeToken::Snapshot() == baseline, "Scene owner leaked");
#ifndef REMI_ASAN
        {
            remi::Scene scene;
            const auto root = scene.Create();
            while (scene.Size() < 32) (void)scene.Create();
            // Fill current capacity before forcing the next growth allocation to fail.
            while (scene.Size() < scene.Memory().capacity) (void)scene.Create();
            const auto prior = scene.Size();
            bool failed = false;
            // Permit MSVC Debug string iterator proxies; fail the large slot-vector allocation.
            try { DenyAllocation fault(1024); (void)scene.Create(); } catch (const std::bad_alloc&) { failed = true; }
            Check(failed && scene.Size() == prior && scene.Alive(root), "Failed growth mutated Scene");
            const auto child = scene.Create();
            Check(scene.SetParent(child,root), "Test setup failed");
            bool destroyed = false;
            { DenyAllocation fault; destroyed = scene.Destroy(root); scene.Clear(); }
            Check(destroyed && !scene.Alive(child), "Allocation-free subtree destruction failed");
            remi::EntityId reused;
            { DenyAllocation fault(1024); reused = scene.Create(); }
            Check(scene.Alive(reused), "Free-list reuse allocated slot storage");
        }
        std::cout << "Allocation-failure rollback and allocation-free cleanup passed\n";
#else
        std::cout << "ASan workload; fault injection disabled to preserve sanitizer allocation tracking\n";
#endif
        Check(remi::LifetimeToken::Snapshot() == baseline, "Final owner balance failed");
        std::cout << "25,600 create/add/clear operations; capacity stable; Reset releases storage; owner balance passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
