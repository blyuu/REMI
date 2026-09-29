#include <remi/core/BuildInfo.hpp>
#include <iostream>

int main() {
    const auto info = remi::GetBuildInfo();
    if (info.version.empty() || info.configuration.empty()) {
        std::cerr << "Missing build metadata\n";
        return 1;
    }
    std::cout << "REMI " << info.version << " (" << info.configuration
              << ") Phase 0 bootstrap OK\n";
    return 0;
}
