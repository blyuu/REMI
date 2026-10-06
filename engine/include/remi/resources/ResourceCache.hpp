#pragma once
#include <remi/core/ResourceHandle.hpp>
#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace remi {
// Main-thread cache. Handles observe; only the cache owns objects. Explicit eviction.
// IDs are never reused, including after Clear, so stale handles cannot revive.
template<class T> class ResourceCache {
public:
    using Handle = ResourceHandle<T>;
    ResourceCache() : owner_(NextOwner()) {}
    ResourceCache(const ResourceCache&) = delete;
    ResourceCache& operator=(const ResourceCache&) = delete;
    template<class Loader> Handle Load(const std::string& key, Loader&& loader) {
        if (key.empty()) throw std::invalid_argument("Empty resource key");
        if (loading_) throw std::logic_error("Reentrant resource loading is unsupported");
        if (const auto found = keys_.find(key); found != keys_.end()) { ++hits_; return {found->second,owner_}; }
        if (next_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Resource IDs exhausted");
        struct Guard { bool& value; Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } } guard(loading_);
        try {
            auto object = std::forward<Loader>(loader)();
            if (!object) throw std::runtime_error("Resource loader returned null");
            const auto id = next_;
            auto inserted = objects_.emplace(id,std::move(object));
            try { keys_.emplace(key,id); } catch (...) { objects_.erase(inserted.first); throw; }
            ++next_; ++loads_; return {id,owner_};
        } catch (...) { ++failures_; throw; }
    }
    // Replace a cached object without changing its handle. Build first so loader
    // failure leaves the old object alive. Any raw pointer from Get is invalidated.
    template<class Loader> Handle Reload(const std::string& key, Loader&& loader) {
        if (key.empty()) throw std::invalid_argument("Empty resource key");
        if (loading_) throw std::logic_error("Reentrant resource loading is unsupported");
        if (!keys_.contains(key) && next_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Resource IDs exhausted");
        struct Guard { bool& value; Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } } guard(loading_);
        try {
            std::unique_ptr<T> object = std::forward<Loader>(loader)();
            if (!object) throw std::runtime_error("Resource loader returned null");
            if (const auto found = keys_.find(key); found != keys_.end()) {
                objects_.at(found->second).swap(object);
                ++reloads_;
                return {found->second,owner_};
            }
            const auto id = next_;
            auto inserted = objects_.emplace(id,std::move(object));
            try { keys_.emplace(key,id); } catch (...) { objects_.erase(inserted.first); throw; }
            ++next_; ++loads_; return {id,owner_};
        } catch (...) { ++failures_; throw; }
    }
    [[nodiscard]] const T* Get(Handle handle) const noexcept {
        if (handle.owner != owner_) return nullptr;
        const auto found = objects_.find(handle.id);
        return found == objects_.end() ? nullptr : found->second.get();
    }
    // Main-thread updates for mutable resources such as dynamic meshes.
    [[nodiscard]] T* GetMutable(Handle handle) noexcept {
        if (handle.owner != owner_) return nullptr;
        const auto found = objects_.find(handle.id);
        return found == objects_.end() ? nullptr : found->second.get();
    }
    bool Unload(Handle handle) {
        if (loading_) throw std::logic_error("Cannot unload during loading");
        if (!Get(handle)) return false;
        for (auto it = keys_.begin(); it != keys_.end(); ++it) if (it->second == handle.id) { keys_.erase(it); break; }
        objects_.erase(handle.id); return true;
    }
    void Clear() {
        if (loading_) throw std::logic_error("Cannot clear during loading");
        keys_.clear(); objects_.clear();
    }
    [[nodiscard]] std::size_t Size() const noexcept { return objects_.size(); }
    [[nodiscard]] std::size_t Hits() const noexcept { return hits_; }
    [[nodiscard]] std::size_t Loads() const noexcept { return loads_; }
    [[nodiscard]] std::size_t Reloads() const noexcept { return reloads_; }
    [[nodiscard]] std::size_t Failures() const noexcept { return failures_; }
private:
    static std::uint64_t NextOwner() {
        auto value = owners_.load();
        for (;;) {
            if (value == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Resource owners exhausted");
            if (owners_.compare_exchange_weak(value,value+1)) return value;
        }
    }
    inline static std::atomic<std::uint64_t> owners_{1};
    std::uint64_t owner_, next_ = 1;
    std::size_t hits_ = 0, loads_ = 0, reloads_ = 0, failures_ = 0;
    bool loading_ = false;
    std::map<std::string,std::uint64_t> keys_;
    std::map<std::uint64_t,std::unique_ptr<T>> objects_;
};
}
