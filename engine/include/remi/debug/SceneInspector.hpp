#pragma once
#include <remi/core/Input.hpp>
#include <remi/debug/DebugOverlay.hpp>
#include <remi/scene/Scene.hpp>

namespace remi::debug {
// Small keyboard inspector. Callers pause simulation while Visible() is true.
class SceneInspector {
public:
    void SetVisible(bool visible) { visible_ = visible; dirty_ = true; if (!visible) overlay_.Clear(); }
    [[nodiscard]] bool Visible() const noexcept { return visible_; }
    [[nodiscard]] EntityId Selected() const noexcept { return selected_; }
    void Update(Scene& scene, const Input& input, float seconds);
    void Draw(Renderer& renderer, const Scene& scene);
    void Clear() noexcept { overlay_.Clear(); selected_ = {}; dirty_ = true; }
private:
    bool visible_ = false, dirty_ = true;
    EntityId selected_;
    unsigned width_ = 0, height_ = 0;
    DebugOverlay overlay_;
};
}
