#include "GameHud.hpp"
#include <remi/core/Log.hpp>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int maskWidth = 1200, maskHeight = 96;
struct HudKey {
    unsigned width = 0, height = 0, coins = 0;
    remi::game::State state = remi::game::State::Playing;
    remi::game::MaterialPreset material = remi::game::MaterialPreset::Silver;
    bool inverted = false, supported = false;
    friend bool operator==(const HudKey&,const HudKey&) = default;
};
const wchar_t* MaterialName(remi::game::MaterialPreset material) {
    switch (material) {
    case remi::game::MaterialPreset::Silver: return L"SILVER";
    case remi::game::MaterialPreset::Original: return L"ORIGINAL";
    case remi::game::MaterialPreset::Gold: return L"GOLD";
    case remi::game::MaterialPreset::Midnight: return L"MIDNIGHT";
    }
    return L"SILVER";
}
}

struct GameHud::Impl {
    std::filesystem::path fontFile;
    bool fontLoaded = false, hasKey = false;
    HudKey key;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    std::uint8_t* mask = nullptr;
    std::unique_ptr<remi::Mesh> mesh;
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    unsigned width = 0, height = 0;

    explicit Impl(const std::filesystem::path& file) : fontFile(file) {
        fontLoaded = AddFontResourceExW(fontFile.c_str(),FR_PRIVATE,nullptr) != 0;
        if (!fontLoaded) remi::Log("Pretendard font unavailable; using Segoe UI fallback");
        dc = CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("Cannot create HUD text DC");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = maskWidth;
        info.bmiHeader.biHeight = -maskHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&mask),nullptr,0);
        if (!bitmap || !mask) throw std::runtime_error("Cannot create HUD glyph bitmap");
        oldBitmap = SelectObject(dc,bitmap);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(255,255,255));
    }
    ~Impl() {
        mesh.reset();
        if (dc && oldBitmap) SelectObject(dc,oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        if (fontLoaded) RemoveFontResourceExW(fontFile.c_str(),FR_PRIVATE,nullptr);
    }
    void Quad(float x,float y,float w,float h,remi::Vec3 color,float depth = .1f) {
        const auto start = static_cast<std::uint32_t>(vertices.size());
        const auto px = [&](float v) { return 2*v/static_cast<float>(width)-1; };
        const auto py = [&](float v) { return 1-2*v/static_cast<float>(height); };
        vertices.push_back({{px(x),py(y),depth},color}); vertices.push_back({{px(x+w),py(y),depth},color});
        vertices.push_back({{px(x),py(y+h),depth},color}); vertices.push_back({{px(x+w),py(y+h),depth},color});
        indices.insert(indices.end(),{start,start+1,start+2,start+2,start+1,start+3});
    }
    void Text(float x,float y,const std::wstring& value,int pixels,remi::Vec3 color,bool centered = false) {
        if (value.empty()) return;
        HFONT font = CreateFontW(-pixels,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,
                                 fontLoaded ? L"Pretendard" : L"Segoe UI");
        if (!font) throw std::runtime_error("Cannot create HUD font");
        const auto oldFont = SelectObject(dc,font);
        std::memset(mask,0,maskWidth*maskHeight*4);
        SIZE size{};
        if (!GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),&size) ||
            !TextOutW(dc,0,0,value.c_str(),static_cast<int>(value.size()))) {
            SelectObject(dc,oldFont); DeleteObject(font);
            throw std::runtime_error("Cannot rasterize HUD text");
        }
        SelectObject(dc,oldFont); DeleteObject(font);
        if (centered) x -= static_cast<float>(size.cx)*.5f;
        const int xLimit = std::min(static_cast<int>(size.cx),maskWidth);
        const int yLimit = std::min(static_cast<int>(size.cy),maskHeight);
        for (int row = 0; row < yLimit; ++row) for (int col = 0; col < xLimit; ++col) {
            const auto index = (row*maskWidth+col)*4;
            const float coverage = static_cast<float>(std::max({mask[index],mask[index+1],mask[index+2]}))/255.f;
            if (coverage < .1f) continue;
            // Text sits on an opaque HUD panel; shade edge pixels for antialiasing.
            const remi::Vec3 background{.027f,.044f,.07f};
            Quad(x+col,y+row,1,1,{background.x+(color.x-background.x)*coverage,
                                  background.y+(color.y-background.y)*coverage,
                                  background.z+(color.z-background.z)*coverage},.01f);
        }
    }
};

GameHud::GameHud(const std::filesystem::path& fontFile) : impl_(std::make_unique<Impl>(fontFile)) {}
GameHud::~GameHud() = default;
void GameHud::Update(remi::Renderer& renderer,unsigned width,unsigned height,const remi::game::GravityGame& game) {
    auto& ui = *impl_;
    const HudKey current{width,height,game.Coins(),game.Status(),game.PlayerMaterial(),game.Inverted(),game.Supported()};
    if (ui.hasKey && current == ui.key) return;
    ui.key = current; ui.hasKey = true; ui.width = width; ui.height = height;
    ui.vertices.clear(); ui.indices.clear();
    if (width < 640 || height < 360) { ui.mesh.reset(); return; }
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const remi::Vec3 panel{.027f,.044f,.07f};
    const remi::Vec3 muted{.48f,.60f,.67f};
    const remi::Vec3 gold{1.f,.66f,.19f};
    ui.Quad(0,0,w,86,panel);
    ui.Quad(0,83,w,3,{.08f,.22f,.27f},.09f);
    ui.Quad(0,h-56,w,56,panel);
    ui.Text(30,17,L"REMI  /  GRAVITY RUN",26,{.90f,.96f,1});
    ui.Text(32,51,L"천장의 코인을 모두 모아 출구로 돌아오세요",17,muted);
    ui.Text(w*.56f,21,L"COINS  " + std::to_wstring(game.Coins()) + L" / " + std::to_wstring(game.TotalCoins()),27,gold);
    ui.Text(w-260,23,std::wstring(L"MATERIAL  ")+MaterialName(game.PlayerMaterial()),20,{.70f,.85f,.94f});
    ui.Text(30,h-42,L"WASD 이동    SPACE 중력 반전    Q 재질 선택    ENTER 재시작    F2 디버그",20,{.77f,.87f,.91f});
    if (game.Status() == remi::game::State::Playing) {
        ui.Text(w-255,53,game.Inverted() ? L"GRAVITY  UP" : L"GRAVITY  DOWN",16,muted);
    } else {
        const float cardWidth = std::min(510.f,w-80.f), x = (w-cardWidth)*.5f, y = (h-204.f)*.5f;
        ui.Quad(x,y,cardWidth,204,panel,.08f);
        ui.Quad(x,y,6,204,game.Status() == remi::game::State::Won ? gold : remi::Vec3{.94f,.30f,.32f},.07f);
        ui.Text(w*.5f,y+25,game.Status() == remi::game::State::Won ? L"FINISH" : L"FELL",48,
                game.Status() == remi::game::State::Won ? gold : remi::Vec3{1.f,.43f,.46f},true);
        ui.Text(w*.5f,y+95,game.Status() == remi::game::State::Won ? L"모든 코인을 모았습니다!" : L"발판에서 떨어졌습니다",23,{.86f,.92f,.96f},true);
        ui.Text(w*.5f,y+145,game.Status() == remi::game::State::Won ? L"ENTER  ·  REPLAY" : L"ENTER  ·  RETRY",22,muted,true);
    }
    ui.mesh = renderer.CreateMesh(ui.vertices,ui.indices);
}
void GameHud::Draw(remi::Renderer& renderer) const { if (impl_->mesh) renderer.Draw(*impl_->mesh,remi::Matrix4::Identity()); }
void GameHud::Clear() noexcept { impl_->mesh.reset(); }
