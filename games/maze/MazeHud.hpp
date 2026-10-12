#pragma once
#include <remi/render/Renderer.hpp>
#include <filesystem>
#include <memory>

enum class MazeHudChoice { None, Retry, Quit };

struct MazeHudStats {
    unsigned fps = 0, draws = 0, shadowDraws = 0;
    bool culling = true, deferred = false;
    friend bool operator==(const MazeHudStats&,const MazeHudStats&) = default;
};

class MazeHud {
public:
    explicit MazeHud(const std::filesystem::path& fontFile);
    ~MazeHud();
    MazeHud(const MazeHud&) = delete;
    MazeHud& operator=(const MazeHud&) = delete;
    void Update(remi::Renderer& renderer,unsigned width,unsigned height,
        bool panelVisible,bool cleared,bool quitSelected,const MazeHudStats& stats);
    void Draw(remi::Renderer& renderer) const;
    void Clear() noexcept;
    [[nodiscard]] static MazeHudChoice HitTest(unsigned width,unsigned height,int x,int y) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
