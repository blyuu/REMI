#include <remi/render/ShaderCache.hpp>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stdexcept>
#include <utility>
#include <fstream>
#include <iterator>

namespace remi {
ShaderCache::ShaderCache(std::filesystem::path file, std::string source)
    : file_(std::move(file)), source_(std::move(source)) {
    if (source_.empty() && !file_.empty()) {
        std::ifstream input(file_, std::ios::binary);
        if (input) observedSource_ = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
}

const std::vector<std::uint8_t>& ShaderCache::Get(ShaderKey key) {
    if (key.shadingModel >= ShadingModel::Count) throw std::invalid_argument("Unsupported shading model");
    if (key.stage == ShaderStage::Vertex) key.shadingModel = ShadingModel::Standard;
    else if (key.stage != ShaderStage::Pixel) throw std::invalid_argument("Unsupported shader stage");
    if (const auto found = entries_.find(key); found != entries_.end()) return found->second;

    const char* entry = key.stage == ShaderStage::Vertex ? "VSMain" : "PSMain";
    const char* profile = key.stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
    const char* model = key.shadingModel == ShadingModel::Unlit ? "1" :
                        key.shadingModel == ShadingModel::Toon ? "2" : "0";
    const D3D_SHADER_MACRO macros[] = {{"SHADING_MODEL", model}, {nullptr, nullptr}};
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT result = source_.empty()
        ? D3DCompileFromFile(file_.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                             entry, profile, flags, 0, &code, &errors)
        : D3DCompile(source_.data(), source_.size(), "ResourceManager shader", macros,
                     nullptr, entry, profile, flags, 0, &code, &errors);
    if (FAILED(result)) {
        const std::string details = errors
            ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
            : "Shader file missing or unreadable";
        throw std::runtime_error(std::string(entry) + ": " + details);
    }
    auto* first = static_cast<const std::uint8_t*>(code->GetBufferPointer());
    std::vector<std::uint8_t> bytes(first, first + code->GetBufferSize());
    const auto [found, inserted] = entries_.emplace(key, std::move(bytes));
    (void)inserted;
    ++compilations_;
    return found->second;
}

void ShaderCache::Invalidate() noexcept { entries_.clear(); }

bool ShaderCache::ReplaceSource(std::string source) {
    if (source.empty()) throw std::invalid_argument("Shader source is empty");
    if (source == source_) return false;
    ShaderCache candidate(file_, std::move(source));
    for (const auto& [key, ignored] : entries_) { (void)ignored; (void)candidate.Get(key); }
    source_.swap(candidate.source_);
    entries_.swap(candidate.entries_);
    compilations_ += candidate.compilations_;
    ++generation_;
    return true;
}

bool ShaderCache::ReloadIfChanged() {
    if (!source_.empty() || file_.empty()) return false;
    std::ifstream input(file_, std::ios::binary);
    if (!input) throw std::runtime_error("Shader file missing or unreadable");
    std::string current{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (current == observedSource_) return false;
    ShaderCache candidate(file_);
    for (const auto& [key, ignored] : entries_) { (void)ignored; (void)candidate.Get(key); }
    std::ifstream verify(file_, std::ios::binary);
    const std::string after{std::istreambuf_iterator<char>(verify), std::istreambuf_iterator<char>()};
    if (!verify || after != current) throw std::runtime_error("Shader file changed during recompilation");
    entries_.swap(candidate.entries_);
    observedSource_.swap(current);
    compilations_ += candidate.compilations_;
    ++generation_;
    return true;
}
}
