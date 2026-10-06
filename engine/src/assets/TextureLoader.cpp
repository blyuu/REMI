#include <remi/assets/TextureLoader.hpp>
#include "stb_image.h"
#include <climits>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace remi::assets {
ImageRGBA DecodeImage(std::span<const std::uint8_t> encoded) {
    if (encoded.empty() || encoded.size() > 128ull * 1024 * 1024 || encoded.size() > INT_MAX)
        throw std::invalid_argument("Invalid encoded image size");
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) * height > 64ull * 1024 * 1024)
        throw std::runtime_error("Image dimensions invalid or exceed 64 megapixels");
    auto* decoded = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (!decoded) {
        stbi_image_free(decoded);
        throw std::runtime_error("Image decode failed");
    }
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(decoded,stbi_image_free);
    ImageRGBA image{static_cast<unsigned>(width), static_cast<unsigned>(height), {}};
    image.pixels.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
    return image;
}
ImageRGBA LoadImage(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Could not open image file");
    const auto length = input.tellg();
    if (length <= 0 || length > 128ll * 1024 * 1024) throw std::runtime_error("Image file too large or empty");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length)))
        throw std::runtime_error("Could not read image file");
    return DecodeImage(bytes);
}
}
