#include <remi/platform/Window.hpp>
#include <remi/core/Lifetime.hpp>
#include <windows.h>
#include <windowsx.h>
#include <stdexcept>
#include <string>

namespace remi {
namespace {
constexpr wchar_t ClassName[] = L"REMI.Phase1.Window";
Key Translate(WPARAM key) noexcept {
    switch (key) {
    case VK_ESCAPE: return Key::Escape; case VK_SPACE: return Key::Space;
    case VK_RETURN: return Key::Enter; case 'W': return Key::W; case 'A': return Key::A;
    case 'S': return Key::S; case 'D': return Key::D; case 'Q': return Key::Q; case 'E': return Key::E;
    case VK_UP: return Key::Up; case VK_DOWN: return Key::Down;
    case VK_LEFT: return Key::Left; case VK_RIGHT: return Key::Right;
    case VK_SHIFT: return Key::Shift; case VK_CONTROL: return Key::Control;
    case VK_F1: return Key::F1; case VK_F2: return Key::F2; case VK_F5: return Key::F5; default: return Key::Count;
    }
}
std::runtime_error WinError(const char* operation) {
    return std::runtime_error(std::string(operation) + " failed: Win32 " + std::to_string(GetLastError()));
}
}

struct Window::Impl {
    LifetimeToken lifetime{OwnedKind::Window};
    HWND handle = nullptr;
    std::wstring clientText;
    Input input;
    unsigned width = 0, height = 0;
    bool focused = false, minimized = false, resizing = false, changed = false, closed = false;
    bool timeReset = true;
    bool graphicsSurface = false;

    ~Impl() { if (handle) DestroyWindow(handle); }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->handle = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        switch (message) {
        case WM_CLOSE: self->closed = true; return 0;
        case WM_NCDESTROY:
            self->handle = nullptr; self->closed = true;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, message, wp, lp);
        case WM_SETFOCUS: self->focused = true; self->timeReset = true; self->input.Reset(); return 0;
        case WM_KILLFOCUS:
            self->focused = false; self->timeReset = true; self->input.Reset();
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_ENTERSIZEMOVE: self->resizing = true; self->timeReset = true; self->input.Reset(); return 0;
        case WM_EXITSIZEMOVE: self->resizing = false; self->timeReset = true; self->input.Reset(); return 0;
        case WM_SIZE:
            self->timeReset = true;
            self->minimized = wp == SIZE_MINIMIZED;
            self->width = LOWORD(lp); self->height = HIWORD(lp); self->changed = true;
            if (self->minimized) self->input.Reset();
            return 0;
        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
            if (self->focused) self->input.SetKey(Translate(wp), message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
            // Keep DefWindowProc handling for Alt+F4 and other system keys.
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) break;
            return 0;
        case WM_MOUSEMOVE:
            if (self->focused) self->input.MoveMouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEWHEEL:
            if (self->focused) self->input.AddWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
            return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
            const Key key = (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) ? Key::MouseLeft :
                ((message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) ? Key::MouseRight : Key::MouseMiddle);
            if (down) { SetFocus(hwnd); SetCapture(hwnd); }
            if (self->focused) self->input.SetKey(key, down);
            if (!self->input.Held(Key::MouseLeft) && !self->input.Held(Key::MouseRight) &&
                !self->input.Held(Key::MouseMiddle) && GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED:
            self->input.SetKey(Key::MouseLeft, false); self->input.SetKey(Key::MouseRight, false);
            self->input.SetKey(Key::MouseMiddle, false); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            if (self->graphicsSurface) { EndPaint(hwnd, &paint); return 0; }
            RECT rect{}; GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220, 230, 240));
            rect.left += 28; rect.top += 28;
            DrawTextW(dc, self->clientText.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX);
            EndPaint(hwnd, &paint); return 0;
        }
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};

Window::Window(const WindowConfig& config) : impl_(std::make_unique<Impl>()) {
    impl_->clientText = config.clientText;
    impl_->graphicsSurface = config.graphicsSurface;
    if (config.width == 0 || config.height == 0 || config.width > 16384 || config.height > 16384)
        throw std::invalid_argument("Window client size must be in [1, 16384]");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Impl::Procedure;
    wc.hInstance = instance; wc.lpszClassName = ClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw WinError("RegisterClassEx");
    RECT rect{0, 0, static_cast<LONG>(config.width), static_cast<LONG>(config.height)};
    if (!AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0)) throw WinError("AdjustWindowRectEx");
    const HWND hwnd = CreateWindowExW(0, ClassName, config.title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, impl_.get());
    if (!hwnd) throw WinError("CreateWindowEx");
    RECT client{}; GetClientRect(hwnd, &client);
    impl_->width = static_cast<unsigned>(client.right); impl_->height = static_cast<unsigned>(client.bottom);
    impl_->changed = true;
    if (config.visible) { ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); }
}
Window::~Window() = default;
bool Window::PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) impl_->closed = true;
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !impl_->closed;
}
void Window::WaitForEvents(unsigned timeoutMilliseconds) const {
    MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMilliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}
void* Window::NativeHandle() const noexcept { return impl_->handle; }
unsigned Window::Width() const noexcept { return impl_->width; }
unsigned Window::Height() const noexcept { return impl_->height; }
bool Window::Focused() const noexcept { return impl_->focused; }
bool Window::Minimized() const noexcept { return impl_->minimized; }
bool Window::Resizing() const noexcept { return impl_->resizing; }
bool Window::Closing() const noexcept { return impl_->closed; }
Input& Window::Inputs() noexcept { return impl_->input; }
bool Window::ConsumeResize(unsigned& width, unsigned& height) noexcept {
    if (!impl_->changed) return false;
    width = impl_->width; height = impl_->height; impl_->changed = false; return true;
}
void Window::RequestClose() noexcept { impl_->closed = true; }
bool Window::ConsumeTimeReset() noexcept {
    const bool reset = impl_->timeReset; impl_->timeReset = false; return reset;
}
void Window::SetTitle(const std::wstring& title) { SetWindowTextW(impl_->handle, title.c_str()); }
std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) throw WinError("GetModuleFileName");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
}
