#pragma once
#include <remi/resources/ResourceCache.hpp>
#include <filesystem>
#include <vector>
namespace remi {
struct FileResource { std::vector<char> bytes; };
using FileHandle = ResourceHandle<FileResource>;
class ResourceManager {
public:
    explicit ResourceManager(std::filesystem::path root, std::size_t maxFileBytes = 64 * 1024 * 1024);
    [[nodiscard]] FileHandle LoadFile(const std::filesystem::path& path);
    [[nodiscard]] FileHandle ReloadFile(const std::filesystem::path& path);
    [[nodiscard]] const FileResource* Get(FileHandle handle) const noexcept { return files_.Get(handle); }
    bool Unload(FileHandle handle) { return files_.Unload(handle); }
    void Clear() { files_.Clear(); }
    [[nodiscard]] const ResourceCache<FileResource>& Files() const noexcept { return files_; }
private:
    [[nodiscard]] FileHandle LoadFileInternal(const std::filesystem::path& path, bool reload);
    std::filesystem::path root_;
    std::size_t limit_;
    ResourceCache<FileResource> files_;
};
}
