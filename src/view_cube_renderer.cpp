#include "model_maker/view_cube_renderer.hpp"

#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace mm {
namespace {

void line(HDC dc, int x1, int y1, int x2, int y2) {
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

template <std::size_t Size>
void polygon(HDC dc, const std::array<CubePoint, Size>& points, COLORREF fill, COLORREF border) {
    std::array<POINT, Size> native{};
    for (std::size_t i = 0; i < Size; ++i) native[i] = {points[i].x, points[i].y};
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, native.data(), static_cast<int>(native.size()));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

const wchar_t* faceLabel(StandardView view) noexcept {
    switch (view) {
    case StandardView::Top: return L"ÜST";
    case StandardView::Bottom: return L"ALT";
    case StandardView::Front: return L"ÖN";
    case StandardView::Back: return L"ARKA";
    case StandardView::Left: return L"SOL";
    case StandardView::Right: return L"SAĞ";
    default: return L"";
    }
}

void drawCube(HDC dc, int width, const POINT& cursor, const Camera& camera) {
    const auto cube = ViewCube::layout(width, camera);
    const auto hovered = ViewCube::hitTest(cursor.x, cursor.y, width, camera);

    RECT panel{cube.centerX - 76, 48, cube.centerX + 76, 238};
    HBRUSH panelBrush = CreateSolidBrush(RGB(20, 26, 37));
    HPEN panelPen = CreatePen(PS_SOLID, 1, RGB(75, 88, 108));
    HGDIOBJ oldBrush = SelectObject(dc, panelBrush);
    HGDIOBJ oldPen = SelectObject(dc, panelPen);
    RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 12, 12);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(panelPen);
    DeleteObject(panelBrush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(244, 247, 252));
    HGDIOBJ previousFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));

    std::array<const ViewCubeFace*, 6> visible{};
    std::size_t visibleCount = 0;
    for (const auto& face : cube.faces)
        if (face.visible) visible[visibleCount++] = &face;
    for (std::size_t i = 0; i < visibleCount; ++i)
        for (std::size_t j = i + 1; j < visibleCount; ++j)
            if (visible[j]->depth < visible[i]->depth) std::swap(visible[i], visible[j]);

    for (std::size_t i = 0; i < visibleCount; ++i) {
        const auto& face = *visible[i];
        const COLORREF normal = face.view == StandardView::Top || face.view == StandardView::Bottom
            ? RGB(112, 133, 155)
            : (face.view == StandardView::Front || face.view == StandardView::Back
                ? RGB(72, 92, 116) : RGB(55, 73, 96));
        polygon(dc, face.points, hovered == face.view ? RGB(42, 155, 220) : normal,
                RGB(210, 222, 235));
    }

    for (std::size_t i = 0; i < visibleCount; ++i) {
        const auto& face = *visible[i];
        long long twiceArea = 0;
        for (std::size_t point = 0; point < face.points.size(); ++point) {
            const auto& a = face.points[point];
            const auto& b = face.points[(point + 1) % face.points.size()];
            twiceArea += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
        }
        if (std::abs(twiceArea) < 700) continue;
        int centerX = 0;
        int centerY = 0;
        for (const auto& point : face.points) {
            centerX += point.x;
            centerY += point.y;
        }
        centerX /= 4;
        centerY /= 4;
        RECT text{centerX - 24, centerY - 10, centerX + 24, centerY + 10};
        DrawTextW(dc, faceLabel(face.view), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const auto drawAxis = [&](const CubePoint& end, COLORREF color, const wchar_t* name) {
        HPEN axisPen = CreatePen(PS_SOLID, 2, color);
        HGDIOBJ previous = SelectObject(dc, axisPen);
        line(dc, cube.axisOrigin.x, cube.axisOrigin.y, end.x, end.y);
        SelectObject(dc, previous);
        DeleteObject(axisPen);
        RECT text{end.x - 7, end.y - 9, end.x + 8, end.y + 9};
        SetTextColor(dc, color);
        DrawTextW(dc, name, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    drawAxis(cube.xAxis, RGB(235, 82, 96), L"X");
    drawAxis(cube.yAxis, RGB(72, 211, 121), L"Y");
    drawAxis(cube.zAxis, RGB(78, 148, 255), L"Z");
    SetTextColor(dc, RGB(244, 247, 252));

    RECT home{cube.homeControl.left, cube.homeControl.top,
              cube.homeControl.right, cube.homeControl.bottom};
    HBRUSH homeBrush = CreateSolidBrush(hovered == StandardView::Isometric
                                        ? RGB(42, 155, 220) : RGB(74, 86, 105));
    FillRect(dc, &home, homeBrush);
    DeleteObject(homeBrush);
    FrameRect(dc, &home, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    DrawTextW(dc, L"ISO", -1, &home, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previousFont);
}

} // namespace

ViewCubeRenderer::~ViewCubeRenderer() {
    if (!backBufferDc_) return;
    if (backBufferBitmap_) {
        SelectObject(backBufferDc_, backBufferDefaultBitmap_);
        DeleteObject(backBufferBitmap_);
    }
    DeleteDC(backBufferDc_);
}

HDC ViewCubeRenderer::ensureBackBuffer(HDC target, int width, int height) const {
    if (!backBufferDc_) {
        backBufferDc_ = CreateCompatibleDC(target);
        if (!backBufferDc_) return nullptr;
        backBufferDefaultBitmap_ = GetCurrentObject(backBufferDc_, OBJ_BITMAP);
    }
    if (backBufferBitmap_ && backBufferWidth_ == width && backBufferHeight_ == height)
        return backBufferDc_;

    HBITMAP replacement = CreateCompatibleBitmap(target, width, height);
    if (!replacement) return nullptr;
    SelectObject(backBufferDc_, replacement);
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    backBufferBitmap_ = replacement;
    backBufferWidth_ = width;
    backBufferHeight_ = height;
    return backBufferDc_;
}

void ViewCubeRenderer::draw(HDC target, const RECT& client, const POINT& cursor,
                            const Camera& camera, IRenderBackend* backend) const {
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    HDC dc = ensureBackBuffer(target, width, height);
    if (!dc) return;

    RECT background{0, 0, width, height};
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(15, 18, 26));
    FillRect(dc, &background, backgroundBrush);
    DeleteObject(backgroundBrush);
    drawCube(dc, width, cursor, camera);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
}

} // namespace mm
