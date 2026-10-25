#include "model_maker/render_backend.hpp"

#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <tuple>

namespace mm {
namespace {

COLORREF toNative(std::uint32_t rgb) noexcept {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

DWORD penStyleFlags(RenderPenStyle style) noexcept {
    switch (style) {
    case RenderPenStyle::Dash: return PS_DASH;
    case RenderPenStyle::Dot: return PS_DOT;
    default: return PS_SOLID;
    }
}

POINT toNative(RenderPoint p) noexcept { return {p.x, p.y}; }

RECT toNative(const RenderRect& r) noexcept { return {r.left, r.top, r.right, r.bottom}; }

void gdiLine(HDC dc, int x1, int y1, int x2, int y2) {
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

} // namespace

class GdiRenderBackend final : public IRenderBackend {
public:
    ~GdiRenderBackend() override { shutdown(); }

    bool initialize(void* windowHandle, int width, int height) override {
        window_ = static_cast<HWND>(windowHandle);
        width_ = width;
        height_ = height;
        return true;
    }

    void shutdown() override {
        releaseBackBuffer();
        for (auto& [key, pen] : pens_) DeleteObject(pen);
        for (auto& [key, brush] : brushes_) DeleteObject(brush);
        for (auto& [key, font] : fonts_) DeleteObject(font);
        pens_.clear();
        brushes_.clear();
        fonts_.clear();
    }

    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
        releaseBackBuffer();
    }

    bool beginFrame(const FrameInfo& info) override {
        if (!window_) return false;
        if (backBufferDc_ && backBufferBitmap_ &&
            backBufferWidth_ == info.width && backBufferHeight_ == info.height) {
            targetDc_ = GetDC(window_);
            return targetDc_ != nullptr;
        }

        releaseBackBuffer();
        HDC windowDc = GetDC(window_);
        if (!windowDc) return false;

        backBufferDc_ = CreateCompatibleDC(windowDc);
        backBufferBitmap_ = CreateCompatibleBitmap(windowDc, info.width, info.height);
        backBufferWidth_ = info.width;
        backBufferHeight_ = info.height;
        backBufferDefaultBitmap_ = static_cast<HBITMAP>(SelectObject(backBufferDc_, backBufferBitmap_));

        targetDc_ = windowDc;
        return true;
    }

    void endFrame() override {
        if (backBufferDc_ && backBufferBitmap_ && targetDc_) {
            BitBlt(targetDc_, 0, 0, backBufferWidth_, backBufferHeight_,
                   backBufferDc_, 0, 0, SRCCOPY);
        }
        if (targetDc_) {
            ReleaseDC(window_, targetDc_);
            targetDc_ = nullptr;
        }
    }

    RenderPenHandle createPen(std::uint32_t color, int widthPixels,
                              RenderPenStyle style) override {
        const COLORREF native = toNative(color);
        HPEN hpen = CreatePen(penStyleFlags(style), std::max(1, widthPixels), native);
        if (!hpen) return {};
        RenderPenHandle handle{reinterpret_cast<std::uintptr_t>(hpen)};
        pens_.emplace(handle.id, hpen);
        return handle;
    }

    RenderBrushHandle createSolidBrush(std::uint32_t color) override {
        HBRUSH hbrush = CreateSolidBrush(toNative(color));
        if (!hbrush) return {};
        RenderBrushHandle handle{reinterpret_cast<std::uintptr_t>(hbrush)};
        brushes_.emplace(handle.id, hbrush);
        return handle;
    }

    RenderFontHandle createFont(int height, bool bold, const wchar_t* faceName) override {
        HFONT hfont = CreateFontW(-height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                                  FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, faceName);
        if (!hfont) return {};
        RenderFontHandle handle{reinterpret_cast<std::uintptr_t>(hfont)};
        fonts_.emplace(handle.id, hfont);
        return handle;
    }

    void deletePen(RenderPenHandle pen) override {
        auto it = pens_.find(pen.id);
        if (it != pens_.end()) {
            DeleteObject(it->second);
            pens_.erase(it);
        }
    }

    void deleteBrush(RenderBrushHandle brush) override {
        auto it = brushes_.find(brush.id);
        if (it != brushes_.end()) {
            DeleteObject(it->second);
            brushes_.erase(it);
        }
    }

    void deleteFont(RenderFontHandle font) override {
        auto it = fonts_.find(font.id);
        if (it != fonts_.end()) {
            DeleteObject(it->second);
            fonts_.erase(it);
        }
    }

    void drawLine(RenderPoint a, RenderPoint b, RenderPenHandle pen) override {
        HGDIOBJ old = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(pen.id));
        gdiLine(backBufferDc_, a.x, a.y, b.x, b.y);
        SelectObject(backBufferDc_, old);
    }

    void drawPolyline(const RenderPoint* points, int count, RenderPenHandle pen) override {
        if (count < 2) return;
        std::vector<POINT> native(count);
        for (int i = 0; i < count; ++i) native[i] = toNative(points[i]);
        HGDIOBJ old = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(pen.id));
        Polyline(backBufferDc_, native.data(), count);
        SelectObject(backBufferDc_, old);
    }

    void drawPolygon(const RenderPoint* points, int count,
                     RenderBrushHandle fill, RenderPenHandle border) override {
        if (count < 3) return;
        std::vector<POINT> native(count);
        for (int i = 0; i < count; ++i) native[i] = toNative(points[i]);
        HGDIOBJ oldBrush = SelectObject(backBufferDc_, reinterpret_cast<HBRUSH>(fill.id));
        HGDIOBJ oldPen = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(border.id));
        Polygon(backBufferDc_, native.data(), count);
        SelectObject(backBufferDc_, oldPen);
        SelectObject(backBufferDc_, oldBrush);
    }

    void drawRectangle(const RenderRect& rect, RenderPenHandle pen) override {
        RECT r = toNative(rect);
        HGDIOBJ oldPen = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(pen.id));
        HGDIOBJ oldBrush = SelectObject(backBufferDc_, GetStockObject(NULL_BRUSH));
        Rectangle(backBufferDc_, r.left, r.top, r.right, r.bottom);
        SelectObject(backBufferDc_, oldBrush);
        SelectObject(backBufferDc_, oldPen);
    }

    void drawFilledRect(const RenderRect& rect, RenderBrushHandle brush) override {
        RECT r = toNative(rect);
        FillRect(backBufferDc_, &r, reinterpret_cast<HBRUSH>(brush.id));
    }

    void drawEllipse(RenderPoint center, int radiusX, int radiusY,
                     RenderPenHandle pen) override {
        HGDIOBJ oldPen = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(pen.id));
        HGDIOBJ oldBrush = SelectObject(backBufferDc_, GetStockObject(NULL_BRUSH));
        Ellipse(backBufferDc_, center.x - radiusX, center.y - radiusY,
                center.x + radiusX + 1, center.y + radiusY + 1);
        SelectObject(backBufferDc_, oldBrush);
        SelectObject(backBufferDc_, oldPen);
    }

    void drawRoundedRect(const RenderRect& rect, int cornerRadius,
                         RenderBrushHandle fill, RenderPenHandle border) override {
        RECT r = toNative(rect);
        HGDIOBJ oldBrush = SelectObject(backBufferDc_, reinterpret_cast<HBRUSH>(fill.id));
        HGDIOBJ oldPen = SelectObject(backBufferDc_, reinterpret_cast<HPEN>(border.id));
        RoundRect(backBufferDc_, r.left, r.top, r.right, r.bottom, cornerRadius, cornerRadius);
        SelectObject(backBufferDc_, oldPen);
        SelectObject(backBufferDc_, oldBrush);
    }

    void drawAlphaPolygon(const RenderPoint* points, int count,
                          std::uint32_t color, int alpha) override {
        if (count < 3) return;
        // Compute bounding box
        LONG left = points[0].x, right = left, top = points[0].y, bottom = top;
        for (int i = 1; i < count; ++i) {
            left = std::min(left, static_cast<LONG>(points[i].x));
            right = std::max(right, static_cast<LONG>(points[i].x));
            top = std::min(top, static_cast<LONG>(points[i].y));
            bottom = std::max(bottom, static_cast<LONG>(points[i].y));
        }
        left = std::clamp<LONG>(left, 0, backBufferWidth_);
        right = std::clamp<LONG>(right, 0, backBufferWidth_);
        top = std::clamp<LONG>(top, 0, backBufferHeight_);
        bottom = std::clamp<LONG>(bottom, 0, backBufferHeight_);
        const int faceWidth = right - left;
        const int faceHeight = bottom - top;
        if (faceWidth <= 0 || faceHeight <= 0) return;

        HDC overlayDc = CreateCompatibleDC(backBufferDc_);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(backBufferDc_, faceWidth, faceHeight);
        HGDIOBJ oldBitmap = SelectObject(overlayDc, overlayBitmap);
        BitBlt(overlayDc, 0, 0, faceWidth, faceHeight, backBufferDc_, left, top, SRCCOPY);

        std::vector<POINT> localPoints(count);
        for (int i = 0; i < count; ++i) {
            localPoints[i] = {points[i].x - left, points[i].y - top};
        }

        HBRUSH brush = CreateSolidBrush(toNative(color));
        HGDIOBJ oldBrush = SelectObject(overlayDc, brush);
        HGDIOBJ oldPen = SelectObject(overlayDc, GetStockObject(NULL_PEN));
        Polygon(overlayDc, localPoints.data(), count);
        SelectObject(overlayDc, oldPen);
        SelectObject(overlayDc, oldBrush);

        BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(alpha), 0};
        AlphaBlend(backBufferDc_, left, top, faceWidth, faceHeight,
                   overlayDc, 0, 0, faceWidth, faceHeight, blend);

        SelectObject(overlayDc, oldBitmap);
        DeleteObject(brush);
        DeleteObject(overlayBitmap);
        DeleteDC(overlayDc);
    }

    void drawAlphaRect(const RenderRect& rect, std::uint32_t color, int alpha) override {
        const int w = rect.width();
        const int h = rect.height();
        if (w <= 0 || h <= 0) return;

        HDC overlayDc = CreateCompatibleDC(backBufferDc_);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(backBufferDc_, w, h);
        HGDIOBJ oldBitmap = SelectObject(overlayDc, overlayBitmap);
        BitBlt(overlayDc, 0, 0, w, h, backBufferDc_, rect.left, rect.top, SRCCOPY);

        RECT local{0, 0, w, h};
        HBRUSH brush = CreateSolidBrush(toNative(color));
        FillRect(overlayDc, &local, brush);

        BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(alpha), 0};
        AlphaBlend(backBufferDc_, rect.left, rect.top, w, h,
                   overlayDc, 0, 0, w, h, blend);

        SelectObject(overlayDc, oldBitmap);
        DeleteObject(brush);
        DeleteObject(overlayBitmap);
        DeleteDC(overlayDc);
    }

    void drawText(int x, int y, const wchar_t* text, std::uint32_t color,
                  RenderFontHandle font) override {
        SetTextColor(backBufferDc_, toNative(color));
        SetBkMode(backBufferDc_, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(backBufferDc_, reinterpret_cast<HFONT>(font.id));
        TextOutW(backBufferDc_, x, y, text, static_cast<int>(wcslen(text)));
        SelectObject(backBufferDc_, oldFont);
    }

    bool presentRasterZoom(const FrameInfo& info) override {
        if (!backBufferDc_ || !backBufferBitmap_ || !targetDc_) return false;
        if (info.rasterZoomFactor <= 0.0) return false;

        const int destX = static_cast<int>(std::lround(info.rasterZoomOffsetX));
        const int destY = static_cast<int>(std::lround(info.rasterZoomOffsetY));
        const int destW = std::max(1, static_cast<int>(std::lround(info.width * info.rasterZoomFactor)));
        const int destH = std::max(1, static_cast<int>(std::lround(info.height * info.rasterZoomFactor)));

        HBRUSH bg = CreateSolidBrush(RGB(198, 224, 246) /* gradient ustu acik mavi */);
        // Fill areas outside the zoomed region (top, bottom, left, right strips)
        RECT top{0, 0, info.width, std::max(0, destY)};
        if (top.bottom > top.top) FillRect(targetDc_, &top, bg);
        RECT bottom{0, std::max(0, destY + destH), info.width, info.height};
        if (bottom.bottom > bottom.top) FillRect(targetDc_, &bottom, bg);
        if (destX > 0) {
            RECT left{0, 0, destX, info.height};
            FillRect(targetDc_, &left, bg);
        }
        if (destX + destW < info.width) {
            RECT right{destX + destW, 0, info.width, info.height};
            FillRect(targetDc_, &right, bg);
        }
        DeleteObject(bg);

        SetStretchBltMode(targetDc_, COLORONCOLOR);
        StretchBlt(targetDc_, destX, destY, destW, destH,
                   backBufferDc_, 0, 0, info.width, info.height, SRCCOPY);
        return true;
    }

    bool isHardwareAccelerated() const noexcept override { return false; }
    const wchar_t* backendName() const noexcept override { return L"GDI"; }

private:
    void releaseBackBuffer() {
        if (backBufferDc_ && backBufferBitmap_) {
            SelectObject(backBufferDc_, backBufferDefaultBitmap_);
            DeleteObject(backBufferBitmap_);
        }
        if (backBufferDc_) {
            if (!backBufferBitmap_) SelectObject(backBufferDc_, backBufferDefaultBitmap_);
            DeleteDC(backBufferDc_);
        }
        backBufferDc_ = nullptr;
        backBufferBitmap_ = nullptr;
        backBufferDefaultBitmap_ = nullptr;
        backBufferWidth_ = 0;
        backBufferHeight_ = 0;
    }

    HWND window_{};
    HDC targetDc_{};
    HDC backBufferDc_{};
    HBITMAP backBufferBitmap_{};
    HBITMAP backBufferDefaultBitmap_{};
    int backBufferWidth_{};
    int backBufferHeight_{};
    int width_{};
    int height_{};
    RenderPenHandle stockPen_{};

    std::map<std::uintptr_t, HPEN> pens_;
    std::map<std::uintptr_t, HBRUSH> brushes_;
    std::map<std::uintptr_t, HFONT> fonts_;
};

std::unique_ptr<IRenderBackend> createGdiRenderBackend() {
    return std::make_unique<GdiRenderBackend>();
}

} // namespace mm
