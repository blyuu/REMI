#pragma once
#include <array>
#include <cstddef>

namespace remi {
enum class Key : std::size_t {
    Escape, Space, Enter, W, A, S, D, Q, E, Up, Down, Left, Right,
    Shift, Control, F1, F2, F3, F5, MouseLeft, MouseRight, MouseMiddle, Count
};

// Edges accumulate until a fixed tick consumes them, even across render frames.
// A press and release between ticks are both retained; Held describes latest state.
class Input {
public:
    void SetKey(Key key, bool down) noexcept {
        const auto index = static_cast<std::size_t>(key);
        if (index >= held_.size() || held_[index] == down) return;
        held_[index] = down;
        (down ? pressed_ : released_)[index] = true;
    }
    [[nodiscard]] bool Held(Key key) const noexcept { return Read(held_, key); }
    [[nodiscard]] bool Pressed(Key key) const noexcept { return Read(pressed_, key); }
    [[nodiscard]] bool Released(Key key) const noexcept { return Read(released_, key); }
    void MoveMouse(int x, int y) noexcept {
        if (mouseKnown_) { deltaX_ += x - mouseX_; deltaY_ += y - mouseY_; }
        mouseX_ = x; mouseY_ = y; mouseKnown_ = true;
    }
    void AddWheel(float steps) noexcept { wheel_ += steps; }
    [[nodiscard]] int MouseX() const noexcept { return mouseX_; }
    [[nodiscard]] int MouseY() const noexcept { return mouseY_; }
    [[nodiscard]] int DeltaX() const noexcept { return deltaX_; }
    [[nodiscard]] int DeltaY() const noexcept { return deltaY_; }
    [[nodiscard]] float Wheel() const noexcept { return wheel_; }
    void ConsumeTick() noexcept {
        pressed_.fill(false); released_.fill(false);
        deltaX_ = deltaY_ = 0; wheel_ = 0;
    }
    void Reset() noexcept {
        held_.fill(false); ConsumeTick(); mouseKnown_ = false;
    }
private:
    using States = std::array<bool, static_cast<std::size_t>(Key::Count)>;
    static bool Read(const States& states, Key key) noexcept {
        const auto index = static_cast<std::size_t>(key);
        return index < states.size() && states[index];
    }
    States held_{}, pressed_{}, released_{};
    int mouseX_ = 0, mouseY_ = 0, deltaX_ = 0, deltaY_ = 0;
    float wheel_ = 0;
    bool mouseKnown_ = false;
};
}
