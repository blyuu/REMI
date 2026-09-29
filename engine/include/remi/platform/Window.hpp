#pragma once
#include <remi/core/Input.hpp>
#include <memory>
#include <string>
#include <filesystem>

namespace remi {
[[nodiscard]] std::filesystem::path ExecutableDirectory();
struct WindowConfig {
    std::wstring title = L"REMI";
    std::wstring clientText; // Temporary GDI status text, supplied by the application.
    unsigned width = 1280, height = 720;
    bool visible = true;
    bool graphicsSurface = false; // Renderer owns client pixels; disable GDI painting.
};

class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    [[nodiscard]] bool PumpMessages();
    void WaitForEvents(unsigned timeoutMilliseconds) const;
    [[nodiscard]] void* NativeHandle() const noexcept;
    [[nodiscard]] unsigned Width() const noexcept;
    [[nodiscard]] unsigned Height() const noexcept;
    [[nodiscard]] bool Focused() const noexcept;
    [[nodiscard]] bool Minimized() const noexcept;
    [[nodiscard]] bool Resizing() const noexcept;
    [[nodiscard]] bool Closing() const noexcept;
    [[nodiscard]] bool ConsumeResize(unsigned& width, unsigned& height) noexcept;
    [[nodiscard]] bool ConsumeTimeReset() noexcept;
    void RequestClose() noexcept;
    void SetTitle(const std::wstring& title);
    [[nodiscard]] Input& Inputs() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
