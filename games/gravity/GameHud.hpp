#pragma once
#include "GravityGame.hpp"
#include <remi/render/Renderer.hpp>
#include <filesystem>
#include <memory>

class GameHud {
public:
    explicit GameHud(const std::filesystem::path& fontFile);
    ~GameHud();
    GameHud(const GameHud&) = delete;
    GameHud& operator=(const GameHud&) = delete;
    void Update(remi::Renderer& renderer, unsigned width, unsigned height, const remi::game::GravityGame& game);
    void Draw(remi::Renderer& renderer) const;
    void Clear() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
