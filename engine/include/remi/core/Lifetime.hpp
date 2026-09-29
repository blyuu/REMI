#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace remi {
enum class OwnedKind : std::size_t { Window, Renderer, Mesh, Scene, Count };
using LiveCounts = std::array<std::size_t, static_cast<std::size_t>(OwnedKind::Count)>;
// Counts engine owners, not allocations/bytes/driver-internal resources.
class LifetimeToken {
public:
    explicit LifetimeToken(OwnedKind kind) noexcept : index_(static_cast<std::size_t>(kind)) { ++counts_[index_]; }
    ~LifetimeToken() { --counts_[index_]; }
    LifetimeToken(const LifetimeToken&) = delete;
    LifetimeToken& operator=(const LifetimeToken&) = delete;
    [[nodiscard]] static LiveCounts Snapshot() noexcept {
        LiveCounts result{};
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = counts_[i].load();
        return result;
    }
private:
    std::size_t index_;
    inline static std::array<std::atomic<std::size_t>, static_cast<std::size_t>(OwnedKind::Count)> counts_{};
};
}
