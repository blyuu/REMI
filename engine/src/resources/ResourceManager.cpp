#include <remi/resources/ResourceManager.hpp>
#include <fstream>
#include <limits>
namespace remi {
ResourceManager::ResourceManager(std::filesystem::path root, std::size_t maxFileBytes)
    : root_(std::filesystem::canonical(root)), limit_(maxFileBytes) {
    if (!std::filesystem::is_directory(root_) || !limit_ || limit_ > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::invalid_argument("Invalid resource root or file size limit");
}
FileHandle ResourceManager::LoadFile(const std::filesystem::path& path) {
    return LoadFileInternal(path,false);
}
FileHandle ResourceManager::ReloadFile(const std::filesystem::path& path) {
    return LoadFileInternal(path,true);
}
FileHandle ResourceManager::LoadFileInternal(const std::filesystem::path& path, bool reload) {
    // Root is a base directory, not a security sandbox. Absolute paths are supported.
    const auto resolved = std::filesystem::weakly_canonical(path.is_absolute() ? path : root_ / path);
    const auto utf8 = resolved.generic_u8string();
    const std::string key(reinterpret_cast<const char*>(utf8.data()),utf8.size());
    const auto read = [&] {
        if (!std::filesystem::is_regular_file(resolved)) throw std::runtime_error("Resource is not a regular file: " + key);
        const auto size = std::filesystem::file_size(resolved);
        if (size > limit_) throw std::runtime_error("Resource exceeds file size limit: " + key);
        std::ifstream input(resolved,std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open resource: " + key);
        auto resource = std::make_unique<FileResource>();
        resource->bytes.resize(static_cast<std::size_t>(size));
        if (size && !input.read(resource->bytes.data(),static_cast<std::streamsize>(size))) throw std::runtime_error("Incomplete resource read: " + key);
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Resource changed while reading: " + key);
        if (input.bad()) throw std::runtime_error("Resource read error: " + key);
        return resource;
    };
    return reload ? files_.Reload(key,read) : files_.Load(key,read);
}
}
