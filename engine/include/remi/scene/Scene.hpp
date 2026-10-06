#pragma once
#include <remi/core/Math.hpp>
#include <remi/core/ResourceHandle.hpp>
#include <remi/core/Lifetime.hpp>
#include <remi/render/Material.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace remi {
struct EntityId {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
    std::uint64_t scene = 0;
    friend bool operator==(const EntityId&, const EntityId&) = default;
};
struct TransformComponent {
    Vec3 position{}, rotation{}; // local Euler angles in radians
    Vec3 scale{1,1,1};
};
// Non-owning typed cache handle; no GPU pointer.
struct MeshComponent {
    MeshHandle mesh;
    bool visible = true;
    MaterialProperties material{};
};
struct SceneMemory {
    std::size_t entities = 0, slots = 0, capacity = 0, reusable = 0, retired = 0;
    std::size_t slotCapacityBytes = 0, nameCapacityCharacters = 0, components = 0;
};

class Scene {
public:
    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    [[nodiscard]] EntityId Create(std::string name = {});
    [[nodiscard]] bool Alive(EntityId id) const noexcept;
    // Destroys the subtree. Old generations never become valid again.
    bool Destroy(EntityId id) noexcept;
    void Clear() noexcept; // Retains capacity for reuse; no allocation.
    void Reset(); // Releases storage and changes Scene identity; invalidates all IDs.
    [[nodiscard]] SceneMemory Memory() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return count_; }
    [[nodiscard]] std::vector<EntityId> Entities() const;
    // Allocation-free traversal. Do not mutate the Scene from the visitor.
    template<class Visitor> void ForEachEntity(Visitor&& visitor) const {
        for (std::size_t index = 0; index < slots_.size(); ++index)
            if (slots_[index].alive) visitor(Id(static_cast<std::uint32_t>(index)));
    }
    [[nodiscard]] std::vector<EntityId> Children(EntityId parent) const;
    [[nodiscard]] std::string_view Name(EntityId id) const;
    void SetName(EntityId id, std::string name);
    [[nodiscard]] EntityId Parent(EntityId id) const;
    // KeepLocal policy. {} detaches; cycles/cross-scene/stale IDs are rejected.
    bool SetParent(EntityId child, EntityId parent);
    [[nodiscard]] Matrix4 WorldMatrix(EntityId id) const;

    template<class T> T* Get(EntityId id) noexcept {
        if (!Alive(id)) return nullptr;
        auto& value = Storage<T>(slots_[id.index]);
        return value ? &*value : nullptr;
    }
    template<class T> const T* Get(EntityId id) const noexcept {
        if (!Alive(id)) return nullptr;
        const auto& value = Storage<T>(slots_[id.index]);
        return value ? &*value : nullptr;
    }
    template<class T> T& Add(EntityId id, T value = {}) {
        Require(id);
        auto& storage = Storage<T>(slots_[id.index]);
        if (storage) throw std::logic_error("Component already exists");
        return storage.emplace(std::move(value));
    }
    template<class T> bool Remove(EntityId id) noexcept {
        if (!Alive(id)) return false;
        auto& value = Storage<T>(slots_[id.index]);
        const bool existed = value.has_value(); value.reset(); return existed;
    }
private:
    struct Slot {
        std::uint32_t generation = 1;
        bool alive = false;
        std::uint32_t nextFree = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t firstChild = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t nextSibling = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t prevSibling = std::numeric_limits<std::uint32_t>::max();
        std::string name;
        EntityId parent;
        std::optional<TransformComponent> transform;
        std::optional<MeshComponent> mesh;
    };
    template<class T, class S> static auto& Storage(S& slot) noexcept {
        if constexpr (std::is_same_v<T, TransformComponent>) return slot.transform;
        else if constexpr (std::is_same_v<T, MeshComponent>) return slot.mesh;
        else static_assert(!std::is_same_v<T, T>, "Unsupported Scene component type");
    }
    void Require(EntityId id) const;
    void UnlinkChild(std::uint32_t index) noexcept;
    void Retire(std::uint32_t index) noexcept;
    [[nodiscard]] EntityId Id(std::uint32_t index) const noexcept;
    std::uint64_t token_ = 0;
    LifetimeToken lifetime_{OwnedKind::Scene};
    std::size_t count_ = 0;
    std::vector<Slot> slots_;
    std::uint32_t freeHead_ = std::numeric_limits<std::uint32_t>::max();
};
}
