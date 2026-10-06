#include <remi/scene/Scene.hpp>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace remi {
namespace {
std::uint64_t NextToken() {
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Scene identity exhausted");
        if (next.compare_exchange_weak(value, value + 1)) return value;
    }
}
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
Scene::Scene() : token_(NextToken()) {}
EntityId Scene::Id(std::uint32_t index) const noexcept { return {index, slots_[index].generation, token_}; }
bool Scene::Alive(EntityId id) const noexcept {
    return id.scene == token_ && id.index < slots_.size() && slots_[id.index].alive && slots_[id.index].generation == id.generation;
}
void Scene::Require(EntityId id) const { if (!Alive(id)) throw std::invalid_argument("Invalid or stale entity ID"); }
EntityId Scene::Create(std::string name) {
    Slot fresh; fresh.name = std::move(name); fresh.alive = true; fresh.transform.emplace();
    std::uint32_t index;
    if (freeHead_ != std::numeric_limits<std::uint32_t>::max()) {
        index = freeHead_; fresh.generation = slots_[index].generation;
        freeHead_ = slots_[index].nextFree;
        static_assert(std::is_nothrow_move_assignable_v<Slot>);
        slots_[index] = std::move(fresh);
    } else {
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("Entity index exhausted");
        index = static_cast<std::uint32_t>(slots_.size()); slots_.push_back(std::move(fresh));
    }
    ++count_; return Id(index);
}
void Scene::Retire(std::uint32_t index) noexcept {
    auto& slot = slots_[index];
    slot.alive = false; slot.name.clear(); slot.parent = {}; slot.transform.reset(); slot.mesh.reset();
    slot.firstChild = slot.nextSibling = slot.prevSibling = std::numeric_limits<std::uint32_t>::max();
    --count_;
    // Exhausted generations permanently retire the index rather than wrap.
    if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
        ++slot.generation; slot.nextFree = freeHead_; freeHead_ = index;
    }
}
void Scene::UnlinkChild(std::uint32_t index) noexcept {
    auto& slot = slots_[index];
    if (slot.parent == EntityId{}) return;
    if (slot.prevSibling != std::numeric_limits<std::uint32_t>::max())
        slots_[slot.prevSibling].nextSibling = slot.nextSibling;
    else
        slots_[slot.parent.index].firstChild = slot.nextSibling;
    if (slot.nextSibling != std::numeric_limits<std::uint32_t>::max())
        slots_[slot.nextSibling].prevSibling = slot.prevSibling;
    slot.parent = {};
    slot.nextSibling = slot.prevSibling = std::numeric_limits<std::uint32_t>::max();
}
bool Scene::Destroy(EntityId id) noexcept {
    if (!Alive(id)) return false;
    // Linked children allow stackless post-order deletion in O(subtree size).
    auto current = id;
    for (;;) {
        const auto child = slots_[current.index].firstChild;
        if (child != std::numeric_limits<std::uint32_t>::max()) { current = Id(child); continue; }
        const auto parent = slots_[current.index].parent;
        UnlinkChild(current.index);
        Retire(current.index);
        if (current == id) break;
        current = parent;
    }
    return true;
}
void Scene::Clear() noexcept {
    for (std::uint32_t index = 0; index < slots_.size(); ++index) if (slots_[index].alive) Retire(index);
}
void Scene::Reset() {
    const auto newToken = NextToken(); // Acquire first: failure leaves this Scene unchanged.
    std::vector<Slot>().swap(slots_);
    token_ = newToken; count_ = 0; freeHead_ = std::numeric_limits<std::uint32_t>::max();
}
SceneMemory Scene::Memory() const noexcept {
    SceneMemory result; result.entities = count_; result.slots = slots_.size(); result.capacity = slots_.capacity();
    result.slotCapacityBytes = slots_.capacity() * sizeof(Slot);
    for (const auto& slot : slots_) {
        if (!slot.alive) {
            // A max-generation slot can still be free once, so count retired below via free-list.
            ++result.retired;
        }
        result.nameCapacityCharacters += slot.name.capacity();
        result.components += static_cast<std::size_t>(slot.transform.has_value()) + static_cast<std::size_t>(slot.mesh.has_value());
    }
    for (auto index = freeHead_; index != std::numeric_limits<std::uint32_t>::max(); index = slots_[index].nextFree) ++result.reusable;
    result.retired -= result.reusable;
    return result;
}
std::vector<EntityId> Scene::Entities() const {
    std::vector<EntityId> result; result.reserve(count_);
    for (std::uint32_t index = 0; index < slots_.size(); ++index) if (slots_[index].alive) result.push_back(Id(index));
    return result;
}
std::vector<EntityId> Scene::Children(EntityId parent) const {
    Require(parent); std::vector<EntityId> result;
    for (auto index = slots_[parent.index].firstChild; index != std::numeric_limits<std::uint32_t>::max();
         index = slots_[index].nextSibling) result.push_back(Id(index));
    std::sort(result.begin(),result.end(),[](EntityId a,EntityId b) { return a.index < b.index; });
    return result;
}
std::string_view Scene::Name(EntityId id) const { Require(id); return slots_[id.index].name; }
void Scene::SetName(EntityId id, std::string name) { Require(id); slots_[id.index].name = std::move(name); }
EntityId Scene::Parent(EntityId id) const { Require(id); return slots_[id.index].parent; }
bool Scene::SetParent(EntityId child, EntityId parent) {
    if (!Alive(child) || (parent != EntityId{} && !Alive(parent))) return false;
    for (auto ancestor = parent; ancestor != EntityId{}; ancestor = slots_[ancestor.index].parent)
        if (ancestor == child) return false;
    UnlinkChild(child.index);
    if (parent != EntityId{}) {
        auto& childSlot = slots_[child.index];
        auto& parentSlot = slots_[parent.index];
        childSlot.parent = parent;
        childSlot.nextSibling = parentSlot.firstChild;
        if (childSlot.nextSibling != std::numeric_limits<std::uint32_t>::max())
            slots_[childSlot.nextSibling].prevSibling = child.index;
        parentSlot.firstChild = child.index;
    }
    return true;
}
Matrix4 Scene::WorldMatrix(EntityId id) const {
    Require(id); auto result = Matrix4::Identity();
    for (auto current = id; current != EntityId{}; current = slots_[current.index].parent) {
        const auto* tr = Get<TransformComponent>(current);
        if (!tr) continue; // Missing Transform is identity, preserving hierarchy.
        if (!Finite(tr->position) || !Finite(tr->rotation) || !Finite(tr->scale)) throw std::invalid_argument("Non-finite Transform");
        result = Multiply(result, TransformMatrix(tr->position, tr->rotation, tr->scale));
    }
    return result;
}
}
