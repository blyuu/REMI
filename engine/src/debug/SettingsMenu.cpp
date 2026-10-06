#include <remi/debug/SettingsMenu.hpp>
#include <remi/core/Log.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace remi::ui {
namespace {
constexpr int maskWidth = 780, maskHeight = 64;
constexpr Vec3 panel{.075f,.105f,.11f};
constexpr Vec3 button{.11f,.16f,.16f};
constexpr Vec3 cream{.94f,.92f,.84f};
constexpr Vec3 muted{.69f,.76f,.72f};
constexpr Vec3 sage{.53f,.72f,.61f};
constexpr Vec3 amber{.88f,.67f,.39f};
}
struct SettingsMenu::Impl {
    std::filesystem::path fontFile;
    std::wstring title;
    std::vector<std::wstring> instructions, status;
    bool cursorHint = false, fontLoaded = false, open = false, dirty = true;
    unsigned width = 0, height = 0;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    std::uint8_t* mask = nullptr;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::unique_ptr<Mesh> mesh;

    Impl(std::filesystem::path file,std::wstring name,std::vector<std::wstring> lines,bool hint)
        : fontFile(std::move(file)),title(std::move(name)),instructions(std::move(lines)),cursorHint(hint) {
        fontLoaded = AddFontResourceExW(fontFile.c_str(),FR_PRIVATE,nullptr) != 0;
        if (!fontLoaded) Log("Noto Sans KR font unavailable; settings use Segoe UI fallback");
        dc = CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("Cannot create settings text DC");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = maskWidth;
        info.bmiHeader.biHeight = -maskHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&mask),nullptr,0);
        if (!bitmap || !mask) throw std::runtime_error("Cannot create settings text bitmap");
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
    void Quad(float x,float y,float w,float h,Vec3 color,float depth = .1f) {
        const auto start = static_cast<std::uint32_t>(vertices.size());
        const auto px = [&](float v) { return 2*v/static_cast<float>(width)-1; };
        const auto py = [&](float v) { return 1-2*v/static_cast<float>(height); };
        vertices.push_back({{px(x),py(y),depth},color});
        vertices.push_back({{px(x+w),py(y),depth},color});
        vertices.push_back({{px(x),py(y+h),depth},color});
        vertices.push_back({{px(x+w),py(y+h),depth},color});
        indices.insert(indices.end(),{start,start+1,start+2,start+2,start+1,start+3});
    }
    void Triangle(float x0,float y0,float x1,float y1,float x2,float y2,Vec3 color,float depth) {
        const auto start = static_cast<std::uint32_t>(vertices.size());
        const auto point = [&](float x,float y) -> Vertex {
            return {{2*x/static_cast<float>(width)-1,1-2*y/static_cast<float>(height),depth},color};
        };
        vertices.push_back(point(x0,y0)); vertices.push_back(point(x1,y1)); vertices.push_back(point(x2,y2));
        indices.insert(indices.end(),{start,start+1,start+2});
    }
    void Gear(float cx,float cy) {
        constexpr float pi = 3.1415926536f;
        for (unsigned i = 0; i < 12; ++i) {
            const float a = 2*pi*static_cast<float>(i)/12;
            const float b = 2*pi*static_cast<float>(i+1)/12;
            Triangle(cx,cy,cx+11*std::cos(a),cy+11*std::sin(a),
                cx+11*std::cos(b),cy+11*std::sin(b),sage,.035f);
        }
        for (unsigned i = 0; i < 8; ++i) {
            const float a = 2*pi*static_cast<float>(i)/8;
            Quad(cx+12*std::cos(a)-2.5f,cy+12*std::sin(a)-2.5f,5,5,sage,.025f);
        }
        for (unsigned i = 0; i < 12; ++i) {
            const float a = 2*pi*static_cast<float>(i)/12;
            const float b = 2*pi*static_cast<float>(i+1)/12;
            Triangle(cx,cy,cx+4.5f*std::cos(a),cy+4.5f*std::sin(a),
                cx+4.5f*std::cos(b),cy+4.5f*std::sin(b),button,.015f);
        }
    }
    void Text(float x,float y,const std::wstring& value,int pixels,Vec3 color,Vec3 background) {
        if (value.empty()) return;
        HFONT font = CreateFontW(-pixels,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,
            fontLoaded ? L"Noto Sans KR" : L"Segoe UI");
        if (!font) throw std::runtime_error("Cannot create settings font");
        const auto oldFont = SelectObject(dc,font);
        std::memset(mask,0,maskWidth*maskHeight*4);
        SIZE size{};
        const bool ok = GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),&size) &&
            TextOutW(dc,0,0,value.c_str(),static_cast<int>(value.size()));
        SelectObject(dc,oldFont); DeleteObject(font);
        if (!ok) throw std::runtime_error("Cannot rasterize settings text");
        const int xLimit = std::min(static_cast<int>(size.cx),maskWidth);
        const int yLimit = std::min(static_cast<int>(size.cy),maskHeight);
        for (int row = 0; row < yLimit; ++row) for (int col = 0; col < xLimit; ++col) {
            const auto index = (row*maskWidth+col)*4;
            const float coverage = static_cast<float>(std::max({mask[index],mask[index+1],mask[index+2]}))/255.f;
            if (coverage < .1f) continue;
            Quad(x+col,y+row,1,1,{background.x+(color.x-background.x)*coverage,
                background.y+(color.y-background.y)*coverage,
                background.z+(color.z-background.z)*coverage},.005f);
        }
    }
    void Build(Renderer& renderer,unsigned w,unsigned h) {
        width = w; height = h; dirty = false;
        vertices.clear(); indices.clear();
        if (w < 480 || h < 320) { mesh.reset(); return; }
        const float fw = static_cast<float>(w), fh = static_cast<float>(h);
        const float bx = fw-150.f;
        Quad(bx,16,132,44,button);
        Quad(bx,58,132,2,sage,.09f);
        Gear(bx+25,38);
        Text(bx+48,26,L"설정",21,cream,button);
        if (cursorHint && !open) Text(bx-135,30,L"ESC · 커서",16,cream,panel);
        if (!status.empty()) {
            const float sh = 22.f+29.f*static_cast<float>(status.size());
            const float sy = fh-sh-18.f;
            Quad(18,sy,475,sh,panel);
            Quad(18,sy,4,sh,amber,.09f);
            for (std::size_t i = 0; i < status.size(); ++i)
                Text(34,sy+11+29.f*static_cast<float>(i),status[i],18,i ? muted : cream,panel);
        }
        if (open) {
            const float px = fw-478.f, py = 72.f, pw = 460.f;
            const float ph = 112.f+40.f*static_cast<float>(instructions.size());
            Quad(px,py,pw,ph,panel,.08f);
            Quad(px,py,5,ph,sage,.07f);
            Text(px+27,py+20,title,25,cream,panel);
            Text(px+27,py+58,L"조작 방법",17,amber,panel);
            Quad(px+27,py+88,pw-54,1,{.24f,.33f,.30f},.07f);
            for (std::size_t i = 0; i < instructions.size(); ++i)
                Text(px+27,py+104+40.f*static_cast<float>(i),instructions[i],19,muted,panel);
        }
        mesh = renderer.CreateMesh(vertices,indices);
    }
};

SettingsMenu::SettingsMenu(std::filesystem::path file,std::wstring title,
    std::vector<std::wstring> lines,bool cursorHint)
    : impl_(std::make_unique<Impl>(std::move(file),std::move(title),std::move(lines),cursorHint)) {}
SettingsMenu::~SettingsMenu() = default;
bool SettingsMenu::HandleClick(const Input& input,unsigned width,unsigned height) {
    if (!input.Pressed(Key::MouseLeft) || width < 480 || height < 320) return false;
    const int x = input.MouseX(), y = input.MouseY();
    if (x >= static_cast<int>(width)-150 && x < static_cast<int>(width)-18 && y >= 16 && y < 60) {
        impl_->open = !impl_->open; impl_->dirty = true; return true;
    }
    return false;
}
void SettingsMenu::SetStatus(std::vector<std::wstring> lines) {
    if (impl_->status == lines) return;
    impl_->status = std::move(lines); impl_->dirty = true;
}
void SettingsMenu::SetOpen(bool open) noexcept { if (impl_->open != open) { impl_->open = open; impl_->dirty = true; } }
bool SettingsMenu::Open() const noexcept { return impl_->open; }
void SettingsMenu::Update(Renderer& renderer,unsigned width,unsigned height) {
    if (impl_->dirty || impl_->width != width || impl_->height != height) impl_->Build(renderer,width,height);
}
void SettingsMenu::Draw(Renderer& renderer) const { if (impl_->mesh) renderer.Draw(*impl_->mesh,Matrix4::Identity()); }
void SettingsMenu::Clear() noexcept { impl_->mesh.reset(); }
}
