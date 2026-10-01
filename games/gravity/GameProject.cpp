#include "GameProject.hpp"
#include "json.hpp"
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace remi::game {
namespace {
Vec3 Vector(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected a three-component vector");
    Vec3 v{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) throw std::invalid_argument("Non-finite project vector");
    return v;
}
std::filesystem::path Path(const nlohmann::json& value, const std::filesystem::path& root) {
    const auto utf8 = value.get<std::string>();
    const auto path = std::filesystem::path(std::u8string(utf8.begin(),utf8.end()));
    if (path.empty()) throw std::invalid_argument("Empty project asset path");
    return std::filesystem::weakly_canonical(path.is_absolute() ? path : root / path);
}
}
GameProject GameProject::Load(const std::filesystem::path& projectFile) {
    const auto file = std::filesystem::absolute(projectFile);
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open game project: " + file.string());
    const auto json = nlohmann::json::parse(input);
    const auto root = file.parent_path();
    GameProject project;
    const auto& assets = json.at("assets");
    project.shader = Path(assets.at("shader"),root);
    project.character = Path(assets.at("character"),root);
    project.font = Path(assets.at("font"),root);
    const auto& level = json.at("level");
    project.level.gravity = level.at("gravity").get<float>();
    project.level.moveSpeed = level.at("moveSpeed").get<float>();
    const auto& physics = level.at("physics");
    project.level.physics.maxStepSeconds = physics.at("maxStepSeconds").get<float>();
    project.level.physics.contactTolerance = physics.at("contactTolerance").get<float>();
    project.level.physics.solverIterations = physics.at("solverIterations").get<unsigned>();
    project.level.physics.restitution = physics.at("restitution").get<float>();
    project.level.playerSpawn = Vector(level.at("playerSpawn"));
    project.level.goalPosition = Vector(level.at("goalPosition"));
    project.level.goalHalfExtent = Vector(level.at("goalHalfExtent"));
    project.level.deathMin = Vector(level.at("deathMin"));
    project.level.deathMax = Vector(level.at("deathMax"));
    const auto& platforms = level.at("platforms");
    const auto& coins = level.at("coins");
    if (!platforms.is_array() || platforms.size() != project.level.platforms.size() ||
        !coins.is_array() || coins.size() != project.level.coinPositions.size()) throw std::invalid_argument("Invalid level object count");
    for (std::size_t i = 0; i < platforms.size(); ++i) {
        project.level.platforms[i] = {Vector(platforms[i].at("position")),Vector(platforms[i].at("halfExtent"))};
        const auto h = project.level.platforms[i].halfExtent;
        if (h.x <= 0 || h.y <= 0 || h.z <= 0) throw std::invalid_argument("Invalid platform extent");
    }
    for (std::size_t i = 0; i < coins.size(); ++i) project.level.coinPositions[i] = Vector(coins[i]);
    const auto h = project.level.goalHalfExtent;
    if (!std::isfinite(project.level.gravity) || project.level.gravity <= 0 ||
        !std::isfinite(project.level.moveSpeed) || project.level.moveSpeed <= 0 ||
        h.x <= 0 || h.y <= 0 || h.z <= 0 ||
        project.level.deathMin.x >= project.level.deathMax.x ||
        project.level.deathMin.y >= project.level.deathMax.y ||
        project.level.deathMin.z >= project.level.deathMax.z) throw std::invalid_argument("Invalid level bounds or movement");
    return project;
}
}
