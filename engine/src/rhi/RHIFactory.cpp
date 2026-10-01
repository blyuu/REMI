#include <remi/rhi/RHIFactory.hpp>
#include "d3d11/D3D11RHI.hpp"
#include <stdexcept>

namespace remi::rhi {
std::unique_ptr<IRHIDevice> CreateDevice(const DeviceDesc& desc) {
    if (desc.backend != Backend::D3D11) throw std::invalid_argument("Unsupported RHI backend");
    return std::make_unique<D3D11Device>(desc);
}
}
