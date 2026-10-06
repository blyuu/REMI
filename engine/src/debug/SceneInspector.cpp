#include <remi/debug/SceneInspector.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace remi::debug {
void SceneInspector::Update(Scene& scene, const Input& input, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid inspector timestep");
    if (input.Pressed(Key::F3)) SetVisible(!visible_);
    if (!visible_) return;
    const auto entities = scene.Entities();
    if (entities.empty()) { selected_ = {}; dirty_ = true; return; }
    auto selected = std::find(entities.begin(),entities.end(),selected_);
    if (selected == entities.end()) { selected = entities.begin(); selected_ = *selected; dirty_ = true; }
    if (!input.Held(Key::Control) && !input.Held(Key::Shift)) {
        const auto direction = int(input.Pressed(Key::Down))-int(input.Pressed(Key::Up));
        if (direction) {
            const auto count = static_cast<std::ptrdiff_t>(entities.size());
            selected_ = entities[static_cast<std::size_t>((selected-entities.begin()+direction+count)%count)];
            dirty_ = true;
        }
    }
    const float horizontal = float(input.Held(Key::Right))-float(input.Held(Key::Left));
    const float vertical = float(input.Held(Key::Up))-float(input.Held(Key::Down));
    Vec3 delta{};
    if (input.Held(Key::Control)) delta = {horizontal*seconds,vertical*seconds,0};
    else if (input.Held(Key::Shift)) delta.z = horizontal*seconds;
    if (auto* transform = scene.Get<TransformComponent>(selected_); transform &&
        (delta.x != 0 || delta.y != 0 || delta.z != 0)) {
        transform->position.x += delta.x; transform->position.y += delta.y; transform->position.z += delta.z;
        dirty_ = true;
    }
}
void SceneInspector::Draw(Renderer& renderer, const Scene& scene) {
    if (!visible_) return;
    if (width_ != renderer.Width() || height_ != renderer.Height()) {
        width_ = renderer.Width(); height_ = renderer.Height(); dirty_ = true;
    }
    if (dirty_) {
        const auto uppercase = [](std::string_view value) {
            std::string result(value.substr(0,40));
            for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return result;
        };
        std::vector<std::string> lines{"SCENE INSPECTOR  F3 CLOSE", "SIMULATION PAUSED - EDITS ARE TEMPORARY",
            "UP DOWN SELECT  CTRL ARROWS MOVE X/Y", "SHIFT LEFT RIGHT MOVE Z  RMB CAMERA"};
        if (scene.Alive(selected_)) {
            lines.push_back("ENTITY " + std::to_string(selected_.index) + " " + uppercase(scene.Name(selected_)));
            const auto parent = scene.Parent(selected_);
            lines.push_back(parent == EntityId{} ? "PARENT NONE" : "PARENT " + uppercase(scene.Name(parent)));
            if (const auto* transform = scene.Get<TransformComponent>(selected_)) {
                std::ostringstream position;
                position << std::fixed << std::setprecision(2) << "LOCAL X " << transform->position.x
                         << " Y " << transform->position.y << " Z " << transform->position.z;
                lines.push_back(position.str());
            }
            lines.push_back("CHILDREN " + std::to_string(scene.Children(selected_).size()) +
                "  SCENE ENTITIES " + std::to_string(scene.Size()));
        } else lines.push_back("SCENE EMPTY");
        overlay_.Update(renderer,width_,height_,lines);
        dirty_ = false;
    }
    overlay_.Draw(renderer);
}
}
