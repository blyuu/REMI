#include <remi/platform/Window.hpp>
#include <remi/render/SceneRenderer.hpp>
#include <remi/resources/ResourceManager.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double Median(std::vector<double> samples) {
    if (samples.empty()) throw std::runtime_error("No valid benchmark samples");
    std::sort(samples.begin(), samples.end());
    if (samples.size()%2) return samples[samples.size()/2];
    return (samples[samples.size()/2-1] + samples[samples.size()/2]) * .5;
}
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void Culling(const std::filesystem::path& shader, std::ostream* csv) {
    remi::WindowConfig windowConfig;
    windowConfig.visible = false; windowConfig.graphicsSurface = true;
    windowConfig.width = windowConfig.height = 64;
    remi::Window window(windowConfig);
    remi::RendererConfig config;
    config.nativeWindow = window.NativeHandle(); config.width = config.height = 64;
    config.shaderFile = shader; config.useWarp = true; config.vsync = false;
    remi::Renderer renderer(config);
    const std::array<remi::Vertex,3> vertices{{
        {{-.05f,-.05f,.5f},{1,1,1}}, {{0,.05f,.5f},{1,1,1}}, {{.05f,-.05f,.5f},{1,1,1}}}};
    const std::array<std::uint32_t,3> indices{0,1,2};
    auto mesh = renderer.CreateMesh(vertices,indices);
    remi::Scene scene;
    for (int i = 0; i < 1000; ++i) {
        const auto entity = scene.Create();
        scene.Add<remi::MeshComponent>(entity, {{1,1},true});
        auto* transform = scene.Get<remi::TransformComponent>(entity);
        transform->position = i < 20 ? remi::Vec3{(i%5)*.2f-.4f,(i/5)*.2f-.3f,0}
                                     : remi::Vec3{5+(i%20)*.2f,(i/20)*.2f,0};
    }
    const auto resolver = [&](remi::MeshHandle) { return mesh.get(); };
    const auto identity = remi::Matrix4::Identity();
    std::vector<double> on, off, onGpu, offGpu;
    std::vector<std::uint8_t> reference;
    unsigned onDraws = 0, offDraws = 0;
    auto frame = [&](bool enabled, bool record) {
        renderer.BeginGpuProfile();
        renderer.BeginShadow(identity); renderer.EndShadow();
        const auto start = Clock::now();
        const auto stats = remi::DrawScene(renderer,scene,identity,resolver,nullptr,enabled);
        const double cpu = Milliseconds(start);
        const auto submitted = renderer.EndGpuProfile();
        // Deliberate synchronization outside CPU timing. This isolates samples,
        // obtains matching GPU query IDs, and checks pixel equivalence.
        const auto pixels = renderer.Readback();
        if (reference.empty()) reference = pixels.rgba;
        Require(pixels.rgba == reference,"Culling changed rendered pixels");
        renderer.Present();
        const auto gpu = renderer.PollGpuProfile();
        if (record) {
            auto& samples = enabled ? on : off;
            samples.push_back(cpu);
            const bool valid = submitted && gpu.valid && gpu.sampleId == submitted;
            if (valid) (enabled ? onGpu : offGpu).push_back(gpu.colorMs);
            if (csv) {
                *csv << "culling," << (enabled ? "on" : "off") << ',' << samples.size() << ',' << cpu << ',';
                if (valid) *csv << gpu.colorMs;
                *csv << ',' << stats.draws << '\n';
            }
            (enabled ? onDraws : offDraws) = stats.draws;
        }
    };
    for (int i = 0; i < 10; ++i) { frame(false,false); frame(true,false); }
    for (int round = 0; round < 10; ++round)
        for (int sample = 0; sample < 10; ++sample) {
            frame(round%2 != 0,true);
            frame(round%2 == 0,true);
        }
    Require(on.size() == 100 && off.size() == 100 && onDraws == 20 && offDraws == 1000,
        "Unexpected culling benchmark draw counts");
    std::cout << "culling,1000 entities,64x64,WARP,100 samples each,"
              << "off_draws=" << offDraws << ",on_draws=" << onDraws
              << ",off_cpu_submit_ms=" << Median(off) << ",on_cpu_submit_ms=" << Median(on)
              << ",off_gpu_color_ms=" << Median(offGpu) << ",on_gpu_color_ms=" << Median(onGpu)
              << ",off_gpu_samples=" << offGpu.size() << ",on_gpu_samples=" << onGpu.size() << '\n';
    mesh.reset();
    const auto audit = renderer.ShutdownAndValidate();
    Require(audit.liveChildren == 0 && audit.priorWarnings == 0, "Renderer audit failed");
}

void FileCache(const std::filesystem::path& fixture, std::ostream* csv) {
    remi::ResourceManager resources(fixture.parent_path());
    const auto filename = fixture.filename();
    auto handle = resources.LoadFile(filename);
    std::vector<double> hit, miss;
    for (int round = 0; round < 20; ++round) {
        for (int sample = 0; sample < 100; ++sample) {
            auto start = Clock::now();
            Require(resources.LoadFile(filename) == handle, "Cache hit changed handle");
            hit.push_back(Milliseconds(start));
            if (csv) *csv << "file_cache,hit," << hit.size() << ',' << hit.back() << ",,\n";
            Require(resources.Unload(handle), "Cache unload failed");
            start = Clock::now();
            handle = resources.LoadFile(filename);
            miss.push_back(Milliseconds(start));
            if (csv) *csv << "file_cache,miss," << miss.size() << ',' << miss.back() << ",,\n";
        }
    }
    std::cout << "file_cache," << resources.Get(handle)->bytes.size() << " bytes,2000 samples each,"
              << "hit_median_ms=" << Median(hit) << ",miss_median_ms=" << Median(miss)
              << ",loads=" << resources.Files().Loads() << ",hits=" << resources.Files().Hits() << '\n';
}

void SubtreeDeletion(std::ostream* csv) {
    std::vector<double> samples;
    for (int repeat = 0; repeat < 20; ++repeat) {
        remi::Scene scene;
        const auto root = scene.Create();
        for (int i = 0; i < 5000; ++i) {
            const auto child = scene.Create();
            Require(scene.SetParent(child,root), "Could not parent benchmark entity");
        }
        const auto start = Clock::now();
        Require(scene.Destroy(root) && scene.Size() == 0, "Subtree deletion failed");
        samples.push_back(Milliseconds(start));
        if (csv) *csv << "subtree_delete,linked," << samples.size() << ',' << samples.back() << ",,\n";
    }
    std::cout << "subtree_delete,5001 entities,20 samples,median_ms=" << Median(samples) << '\n';
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 3 || argc == 4, "Usage: REMIBenchmarks <Basic.hlsl> <fixture.glb> [samples.csv]");
        std::ofstream csv;
        if (argc == 4) {
            csv.open(std::filesystem::path(argv[3]));
            Require(static_cast<bool>(csv),"Could not open benchmark CSV");
            csv << std::fixed << std::setprecision(6) << "benchmark,variant,sample,cpu_ms,gpu_color_ms,draws\n";
        }
        std::cout << std::fixed << std::setprecision(4);
#ifdef _DEBUG
        std::cout << "configuration=Debug\n";
#else
        std::cout << "configuration=Release\n";
#endif
        auto* output = csv.is_open() ? &csv : nullptr;
        Culling(argv[1],output); FileCache(argv[2],output); SubtreeDeletion(output);
        if (csv.is_open()) { csv.flush(); Require(static_cast<bool>(csv),"Benchmark CSV write failed"); }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
