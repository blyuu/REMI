#include <remi/scene/Scene.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("CSV output path required");
        std::ofstream csv{std::filesystem::path(argv[1])};
        if (!csv) throw std::runtime_error("Could not open CSV");
        csv << std::fixed << std::setprecision(6) << "variant,sample,entities,cpu_ms\n";
        std::vector<double> samples;
        for (unsigned repeat = 0; repeat < 22; ++repeat) {
            remi::Scene scene;
            const auto root = scene.Create();
            for (unsigned child = 0; child < 5000; ++child)
                if (!scene.SetParent(scene.Create(),root)) throw std::runtime_error("Could not parent entity");
            const auto start = std::chrono::steady_clock::now();
            const bool deleted = scene.Destroy(root);
            const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (!deleted || scene.Size()) throw std::runtime_error("Subtree deletion failed");
            if (repeat >= 2) {
                samples.push_back(ms);
                csv << BENCHMARK_VARIANT << ',' << samples.size() << ",5001," << ms << '\n';
            }
        }
        csv.flush();
        if (!csv) throw std::runtime_error("CSV write failed");
        std::sort(samples.begin(),samples.end());
        std::cout << std::fixed << std::setprecision(6) << BENCHMARK_VARIANT
                  << ": 5001 entities, median " << (samples[9]+samples[10])*.5 << " ms\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
