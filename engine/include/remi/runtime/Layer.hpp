#pragma once
#include <remi/platform/Window.hpp>

namespace remi {
// Application owns layers. Attach/detach are called once, in stack order/reverse order.
class Layer {
public:
    virtual ~Layer() = default;
    virtual void OnAttach(Window&) {}
    virtual void OnResize(unsigned, unsigned) {}
    virtual void OnFixedUpdate(Window&, const Input&, double) {}
    virtual void OnFrame(Window&, double, double) {}
    virtual void OnDetach() noexcept {}
};
}
