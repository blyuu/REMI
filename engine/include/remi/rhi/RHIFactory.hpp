#pragma once
#include <remi/rhi/RHI.hpp>
#include <memory>

namespace remi::rhi {
[[nodiscard]] std::unique_ptr<IRHIDevice> CreateDevice(const DeviceDesc& desc);
}
