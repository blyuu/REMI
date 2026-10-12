#include <remi/assets/GltfPreparation.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(),value.end()};
}
std::uint64_t Fingerprint(const std::vector<remi::assets::PreparedGltfPrimitive>& data) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& primitive : data) {
        hash = (hash ^ primitive.vertices.size()) * 1099511628211ull;
        hash = (hash ^ primitive.indices.size()) * 1099511628211ull;
        for (const auto index : primitive.indices)
            hash = (hash ^ index) * 1099511628211ull;
        for (const auto& vertex : primitive.vertices) {
            for (float component : {vertex.position.x,vertex.position.y,vertex.position.z,
                                    vertex.color.x,vertex.color.y,vertex.color.z,
                                    vertex.normal.x,vertex.normal.y,vertex.normal.z,
                                    vertex.uv.x,vertex.uv.y})
                hash = (hash ^ std::bit_cast<std::uint32_t>(component)) * 1099511628211ull;
        }
    }
    return hash;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || argc > 5) {
        std::wcerr << L"Usage: REMIGltfPrepBenchmark <model.glb> <output.csv> [repeats=30] [workers=default]\n";
        return 2;
    }
    try {
        const auto sourcePath = std::filesystem::path(argv[1]);
        const auto outputPath = std::filesystem::path(argv[2]);
        const unsigned repeats = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 30u;
        const unsigned workers = argc > 4 ? static_cast<unsigned>(std::stoul(argv[4])) : 0u;
        if (repeats < 5 || repeats > 1000 || workers > 64) throw std::invalid_argument("Invalid repeats or workers");
        gltf::Model model;
        if (!gltf::Load(Utf8(sourcePath),model) || model.primitives.empty())
            throw std::runtime_error("Could not load glTF primitives");
        remi::JobSystem jobs(workers);
        std::ofstream csv(outputPath,std::ios::trunc);
        if (!csv) throw std::runtime_error("Could not open CSV output");
        csv << "source,primitive_copies,source_primitives,vertices,indices,workers,grain,run,mode,cpu_prepare_ms,fingerprint\n";
        // Copies reuse the exact imported source primitive data; only the number of
        // independent conversion tasks changes. No parsing, texture decode or GPU upload is timed.
        for (unsigned copies : {1u,4u,16u}) {
            constexpr std::size_t maxPreparedBytes=512ull*1024ull*1024ull;
            const auto bytesPerCopy=model.TotalVertices()*sizeof(remi::Vertex)+
                model.TotalIndices()*sizeof(std::uint32_t);
            if (bytesPerCopy && copies>maxPreparedBytes/bytesPerCopy) {
                std::cout << "copies=" << copies << " skipped: prepared data exceeds 512 MiB\n";
                continue;
            }
            std::vector<const gltf::Primitive*> sources;
            sources.reserve(model.primitives.size()*copies);
            for (unsigned copy=0; copy<copies; ++copy)
                for (const auto& primitive : model.primitives) sources.push_back(&primitive);
            const auto grain = std::max<std::size_t>(1,sources.size()/(jobs.WorkerCount()*4));
            const auto vertices = model.TotalVertices()*copies;
            const auto indices = model.TotalIndices()*copies;
            std::uint64_t expected=0;
            for (unsigned run=0; run<repeats+2; ++run) {
                // Alternate order to reduce thermal/cache order bias. First two runs warm up.
                for (unsigned order=0; order<2; ++order) {
                    const bool parallel = (run+order)%2 == 1;
                    const auto start=Clock::now();
                    const auto prepared=remi::assets::PrepareGltfPrimitives(sources,parallel ? &jobs : nullptr,grain);
                    const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
                    const auto hash=Fingerprint(prepared);
                    if (expected && hash != expected) throw std::runtime_error("Sequential/parallel output differs");
                    expected=hash;
                    if (run<2) continue;
                    csv << '"' << Utf8(sourcePath.filename()) << "\"," << copies << ',' << model.primitives.size()
                        << ',' << vertices << ',' << indices << ',' << jobs.WorkerCount() << ',' << grain
                        << ',' << (run-2) << ',' << (parallel ? "parallel" : "sequential") << ','
                        << std::fixed << std::setprecision(6) << ms << ',' << hash << '\n';
                }
            }
            std::cout << "copies=" << copies << " primitives=" << sources.size()
                      << " vertices=" << vertices << " grain=" << grain << " verified\n";
        }
        if (!csv) throw std::runtime_error("CSV write failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
