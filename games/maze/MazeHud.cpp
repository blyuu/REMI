#include "MazeHud.hpp"
#include <remi/core/Log.hpp>
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int maskWidth=720, maskHeight=80;
constexpr remi::Vec3 black{.018f,.018f,.018f};
constexpr remi::Vec3 white{.96f,.96f,.96f};
constexpr remi::Vec3 gray{.72f,.72f,.72f};
constexpr remi::Vec3 border{.62f,.62f,.62f};

struct Card {
    float x=0,y=0,w=0,h=0,buttonW=0;
};
Card ClearCard(unsigned width,unsigned height) {
    const float w=std::min(520.f,static_cast<float>(width)-40.f);
    return {(static_cast<float>(width)-w)*.5f,
        (static_cast<float>(height)-236.f)*.5f,w,236.f,(w-72.f)*.5f};
}
}

struct MazeHud::Impl {
    std::filesystem::path fontFile;
    bool fontLoaded=false,hasState=false;
    unsigned width=0,height=0;
    bool panel=false,cleared=false,quitSelected=false;
    MazeHudStats stats{};
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ oldBitmap=nullptr;
    std::uint8_t* mask=nullptr;
    std::unique_ptr<remi::Mesh> mesh;
    std::vector<remi::Vertex> vertices;
    std::vector<std::uint32_t> indices;

    explicit Impl(const std::filesystem::path& file):fontFile(file) {
        fontLoaded=AddFontResourceExW(fontFile.c_str(),FR_PRIVATE,nullptr)!=0;
        if (!fontLoaded) remi::Log("Maze HUD font unavailable; using Segoe UI fallback");
        dc=CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("Cannot create Maze HUD text DC");
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=maskWidth;
        info.bmiHeader.biHeight=-maskHeight;
        info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&mask),nullptr,0);
        if (!bitmap || !mask) throw std::runtime_error("Cannot create Maze HUD glyph bitmap");
        oldBitmap=SelectObject(dc,bitmap);
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
    void Quad(float x,float y,float w,float h,remi::Vec3 color,float depth=.1f) {
        const auto start=static_cast<std::uint32_t>(vertices.size());
        const auto px=[&](float v) { return 2.f*v/static_cast<float>(width)-1.f; };
        const auto py=[&](float v) { return 1.f-2.f*v/static_cast<float>(height); };
        vertices.push_back({{px(x),py(y),depth},color});
        vertices.push_back({{px(x+w),py(y),depth},color});
        vertices.push_back({{px(x),py(y+h),depth},color});
        vertices.push_back({{px(x+w),py(y+h),depth},color});
        indices.insert(indices.end(),{start,start+1,start+2,start+2,start+1,start+3});
    }
    void Outline(float x,float y,float w,float h) {
        Quad(x,y,w,1.f,border,.08f); Quad(x,y+h-1.f,w,1.f,border,.08f);
        Quad(x,y,1.f,h,border,.08f); Quad(x+w-1.f,y,1.f,h,border,.08f);
    }
    void Text(float x,float y,const std::wstring& value,int pixels,
              remi::Vec3 color,remi::Vec3 background,bool centered=false) {
        if (value.empty()) return;
        HFONT font=CreateFontW(-pixels,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH,fontLoaded?L"Noto Sans KR":L"Segoe UI");
        if (!font) throw std::runtime_error("Cannot create Maze HUD font");
        const auto oldFont=SelectObject(dc,font);
        std::memset(mask,0,maskWidth*maskHeight*4);
        SIZE extent{};
        const bool rasterized=GetTextExtentPoint32W(dc,value.c_str(),
            static_cast<int>(value.size()),&extent) &&
            TextOutW(dc,0,0,value.c_str(),static_cast<int>(value.size()));
        SelectObject(dc,oldFont); DeleteObject(font);
        if (!rasterized) throw std::runtime_error("Cannot rasterize Maze HUD text");
        if (centered) x-=static_cast<float>(extent.cx)*.5f;
        const int textWidth=std::min(static_cast<int>(extent.cx),maskWidth);
        const int textHeight=std::min(static_cast<int>(extent.cy),maskHeight);
        for (int row=0; row<textHeight; ++row) for (int col=0; col<textWidth; ++col) {
            const auto index=(row*maskWidth+col)*4;
            const float coverage=static_cast<float>(std::max({mask[index],mask[index+1],mask[index+2]}))/255.f;
            if (coverage<.1f) continue;
            Quad(x+static_cast<float>(col),y+static_cast<float>(row),1.f,1.f,
                {background.x+(color.x-background.x)*coverage,
                 background.y+(color.y-background.y)*coverage,
                 background.z+(color.z-background.z)*coverage},.005f);
        }
    }
    void Build(remi::Renderer& renderer) {
        vertices.clear(); indices.clear();
        if (width<480 || height<320 || (!panel && !cleared)) { mesh.reset(); return; }
        const float w=static_cast<float>(width);
        if (cleared) {
            const auto card=ClearCard(width,height);
            Quad(card.x,card.y,card.w,card.h,black);
            Outline(card.x,card.y,card.w,card.h);
            Text(w*.5f,card.y+31.f,L"MAZE CLEAR",40,white,black,true);
            Text(w*.5f,card.y+89.f,L"ANCIENT RUINS ESCAPED",18,gray,black,true);
            const float by=card.y+151.f;
            const float left=card.x+24.f, right=card.x+card.w-24.f-card.buttonW;
            const auto button=[&](float x,float bw,const wchar_t* label,bool selected) {
                Quad(x,by,bw,54.f,selected?white:black,.07f);
                if (!selected) Outline(x,by,bw,54.f);
                Text(x+bw*.5f,by+12.f,label,22,selected?black:white,
                    selected?white:black,true);
            };
            button(left,card.buttonW,L"RETRY",!quitSelected);
            button(right,card.buttonW,L"QUIT",quitSelected);
        } else {
            constexpr float panelW=380.f,panelH=246.f;
            const float x=w-panelW-20.f,y=20.f;
            Quad(x,y,panelW,panelH,black);
            Outline(x,y,panelW,panelH);
            Text(x+20.f,y+15.f,L"REMI  /  MAZE ESCAPE",23,white,black);
            Quad(x+20.f,y+53.f,panelW-40.f,1.f,border,.08f);
            Text(x+20.f,y+67.f,L"WASD  MOVE     SHIFT  RUN",16,white,black);
            Text(x+20.f,y+94.f,L"MOUSE  ORBIT     WHEEL  ZOOM",16,gray,black);
            Text(x+20.f,y+121.f,L"F1  CULL     F3  INSPECT",16,gray,black);
            Text(x+20.f,y+148.f,L"F4  WIRE     F6  RENDER PATH",16,gray,black);
            Quad(x+20.f,y+180.f,panelW-40.f,1.f,border,.08f);
            Text(x+20.f,y+189.f,L"FPS  "+std::to_wstring(stats.fps)+
                L"     DRAWS  "+std::to_wstring(stats.draws),17,white,black);
            Text(x+20.f,y+215.f,std::wstring(stats.deferred?L"DEFERRED":L"FORWARD")+
                L"     CULL  "+(stats.culling?L"ON":L"OFF")+
                L"     SHADOW  "+std::to_wstring(stats.shadowDraws),15,gray,black);
        }
        mesh=renderer.CreateMesh(vertices,indices);
    }
};

MazeHud::MazeHud(const std::filesystem::path& file):impl_(std::make_unique<Impl>(file)) {}
MazeHud::~MazeHud()=default;
void MazeHud::Update(remi::Renderer& renderer,unsigned width,unsigned height,
    bool panelVisible,bool cleared,bool quitSelected,const MazeHudStats& stats) {
    auto& ui=*impl_;
    if (ui.hasState && ui.width==width && ui.height==height && ui.panel==panelVisible &&
        ui.cleared==cleared && ui.quitSelected==quitSelected && ui.stats==stats) return;
    ui.hasState=true; ui.width=width; ui.height=height;
    ui.panel=panelVisible; ui.cleared=cleared; ui.quitSelected=quitSelected; ui.stats=stats;
    ui.Build(renderer);
}
void MazeHud::Draw(remi::Renderer& renderer) const {
    if (impl_->mesh) renderer.Draw(*impl_->mesh,remi::Matrix4::Identity());
}
void MazeHud::Clear() noexcept { impl_->mesh.reset(); impl_->hasState=false; }
MazeHudChoice MazeHud::HitTest(unsigned width,unsigned height,int x,int y) noexcept {
    if (width<480 || height<320) return MazeHudChoice::None;
    const auto card=ClearCard(width,height);
    const float px=static_cast<float>(x),py=static_cast<float>(y),by=card.y+151.f;
    if (py<by || py>by+54.f) return MazeHudChoice::None;
    if (px>=card.x+24.f && px<=card.x+24.f+card.buttonW) return MazeHudChoice::Retry;
    const float right=card.x+card.w-24.f-card.buttonW;
    if (px>=right && px<=right+card.buttonW) return MazeHudChoice::Quit;
    return MazeHudChoice::None;
}
