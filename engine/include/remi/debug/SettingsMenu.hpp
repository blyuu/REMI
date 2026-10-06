#pragma once
#include <remi/core/Input.hpp>
#include <remi/render/Renderer.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace remi::ui {
// Shared in-game controls panel. The gear remains visible; instructions appear
// only when it is clicked. Text is rasterized with the supplied Noto Sans KR font.
class SettingsMenu {
public:
    SettingsMenu(std::filesystem::path fontFile, std::wstring title,
        std::vector<std::wstring> instructions, bool cursorHint = false);
    ~SettingsMenu();
    SettingsMenu(const SettingsMenu&) = delete;
    SettingsMenu& operator=(const SettingsMenu&) = delete;
    bool HandleClick(const Input& input, unsigned width, unsigned height);
    void SetStatus(std::vector<std::wstring> lines);
    void SetOpen(bool open) noexcept;
    [[nodiscard]] bool Open() const noexcept;
    void Update(Renderer& renderer, unsigned width, unsigned height);
    void Draw(Renderer& renderer) const;
    void Clear() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
