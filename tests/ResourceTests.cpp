#include <remi/resources/ResourceManager.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Tracked {
    inline static int alive = 0;
    Tracked() { ++alive; }
    ~Tracked() { --alive; }
};
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("remi-resource-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { if (!std::filesystem::create_directory(root)) throw std::runtime_error("Fixture collision"); }
    ~Fixture() { std::error_code ec; std::filesystem::remove(root/"asset.bin",ec); std::filesystem::remove(root,ec); }
    void Write(const std::string& text) { std::ofstream out(root/"asset.bin",std::ios::binary); out << text; if (!out) throw std::runtime_error("Fixture write failed"); }
};
int main() {
    try {
        {
            remi::ResourceCache<Tracked> cache, other;
            unsigned calls = 0;
            const auto load = [&] { ++calls; return std::make_unique<Tracked>(); };
            const auto first = cache.Load("one",load);
            Check(cache.Load("one",load) == first && calls == 1 && Tracked::alive == 1, "Duplicate resource loaded");
            Check(!other.Get(first), "Foreign handle accepted");
            bool failed = false;
            try { (void)cache.Load("failure",[]() -> std::unique_ptr<Tracked> { throw std::runtime_error("Expected failure"); }); } catch (...) { failed = true; }
            Check(failed && cache.Size() == 1 && cache.Failures() == 1, "Failure poisoned cache");
            const auto recovered = cache.Load("failure",load);
            Check(cache.Get(recovered) && cache.Unload(first) && !cache.Unload(first) && !cache.Get(first), "Unload failed");
            const auto replacement = cache.Load("one",load);
            Check(replacement != first && !cache.Get(first), "Stale ID revived");
            cache.Clear();
            Check(!cache.Get(replacement) && Tracked::alive == 0, "Clear leaked resource");
            for (unsigned i = 0; i < 1000; ++i) { const auto id = cache.Load("cycle",load); cache.Clear(); Check(!cache.Get(id), "Clear revived handle"); }
            failed = false;
            try { (void)cache.Load("null",[] { return std::unique_ptr<Tracked>{}; }); } catch (...) { failed = true; }
            Check(failed && cache.Size() == 0, "Null resource accepted");
            failed = false;
            try { (void)cache.Load("recursive",[&] { cache.Clear(); return load(); }); } catch (...) { failed = true; }
            Check(failed && cache.Size() == 0, "Reentrant mutation accepted");
            (void)cache.Load("destructor-owned",load);
            Check(Tracked::alive == 1, "Destructor fixture missing");
        }
        Check(Tracked::alive == 0, "Cache destructor leaked");
        Fixture fixture;
        remi::ResourceManager files(fixture.root,16);
        bool failed = false;
        try { (void)files.LoadFile("asset.bin"); } catch (...) { failed = true; }
        Check(failed && files.Files().Size() == 0, "Missing file cached");
        fixture.Write(std::string("abc\0xyz",7));
        const auto file = files.LoadFile("asset.bin");
        Check(files.LoadFile("./asset.bin") == file && files.LoadFile(fixture.root/"asset.bin") == file, "Path aliases duplicated");
        Check(files.Get(file)->bytes.size() == 7 && files.Get(file)->bytes[3] == '\0', "Binary content corrupted");
        fixture.Write("updated");
        Check(files.Get(file)->bytes[0] == 'a', "Cached data changed implicitly");
        Check(files.Unload(file), "File unload failed");
        const auto updated = files.LoadFile("asset.bin");
        Check(updated != file && !files.Get(file) && files.Get(updated)->bytes[0] == 'u', "Explicit reload failed");
        files.Clear(); fixture.Write(std::string(17,'x')); failed = false;
        try { (void)files.LoadFile("asset.bin"); } catch (...) { failed = true; }
        Check(failed && files.Files().Size() == 0, "Oversized file accepted");
        fixture.Write("");
        Check(files.Get(files.LoadFile("asset.bin"))->bytes.empty(), "Empty binary file rejected");
        std::cout << "Cache deduplication, 1000 unload cycles, foreign/stale handles, failure retry, binary IO and size limits passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
