#include "model_maker/renderer.hpp"
#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>

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

void drawViewCube(HDC dc, int viewportWidth, const POINT& cursor) {
    const auto cube = ViewCube::layout(viewportWidth);
    const auto hovered = ViewCube::hitTest(cursor.x, cursor.y, viewportWidth);
    const auto faceColor = [&](StandardView view, COLORREF normal) {
        return hovered == view ? RGB(42, 155, 220) : normal;
    };

    RECT panel{cube.centerX - 74, 56, cube.centerX + 54, 238};
    HBRUSH panelBrush = CreateSolidBrush(RGB(20, 26, 37));
    HGDIOBJ oldBrush = SelectObject(dc, panelBrush);
    HPEN panelPen = CreatePen(PS_SOLID, 1, RGB(75, 88, 108));
    HGDIOBJ oldPen = SelectObject(dc, panelPen);
    RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 12, 12);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(panelPen);
    DeleteObject(panelBrush);

    polygon(dc, cube.topFace, faceColor(StandardView::Top, RGB(116, 137, 158)), RGB(210, 222, 235));
    polygon(dc, cube.frontFace, faceColor(StandardView::Front, RGB(72, 92, 116)), RGB(196, 211, 226));
    polygon(dc, cube.rightFace, faceColor(StandardView::Right, RGB(55, 73, 96)), RGB(196, 211, 226));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(244, 247, 252));
    RECT topLabel{cube.centerX - 24, 92, cube.centerX + 24, 111};
    RECT frontLabel{cube.centerX - 37, 132, cube.centerX, 151};
    RECT rightLabel{cube.centerX, 132, cube.centerX + 37, 151};
    DrawTextW(dc, L"ÜST", -1, &topLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"ÖN", -1, &frontLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"SAĞ", -1, &rightLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const auto controlColor = [&](StandardView view) {
        return hovered == view ? RGB(42, 155, 220) : RGB(74, 86, 105);
    };
    const std::array<CubePoint, 3> leftArrow{{
        {cube.leftControl.left, (cube.leftControl.top + cube.leftControl.bottom) / 2},
        {cube.leftControl.right, cube.leftControl.top}, {cube.leftControl.right, cube.leftControl.bottom}}};
    const std::array<CubePoint, 3> backArrow{{
        {(cube.backControl.left + cube.backControl.right) / 2, cube.backControl.top},
        {cube.backControl.left, cube.backControl.bottom}, {cube.backControl.right, cube.backControl.bottom}}};
    const std::array<CubePoint, 3> bottomArrow{{
        {cube.bottomControl.left, cube.bottomControl.top}, {cube.bottomControl.right, cube.bottomControl.top},
        {(cube.bottomControl.left + cube.bottomControl.right) / 2, cube.bottomControl.bottom}}};
    polygon(dc, leftArrow, controlColor(StandardView::Left), RGB(176, 190, 210));
    polygon(dc, backArrow, controlColor(StandardView::Back), RGB(176, 190, 210));
    polygon(dc, bottomArrow, controlColor(StandardView::Bottom), RGB(176, 190, 210));

    RECT home{cube.homeControl.left, cube.homeControl.top, cube.homeControl.right, cube.homeControl.bottom};
    HBRUSH homeBrush = CreateSolidBrush(controlColor(StandardView::Isometric));
    FillRect(dc, &home, homeBrush);
    DeleteObject(homeBrush);
    FrameRect(dc, &home, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    DrawTextW(dc, L"ISO", -1, &home, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
}

RECT Renderer::canvasRect(const RECT& client) noexcept {
    return client;
}

POINT Renderer::to2DScreen(const Vec3& point, const RECT& canvas) noexcept {
    constexpr double scale = 60.0;
    return {static_cast<LONG>((canvas.left + canvas.right) * 0.5 + point.x * scale),
            static_cast<LONG>((canvas.top + canvas.bottom) * 0.5 - point.y * scale)};
}

void Renderer::drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, text, static_cast<int>(wcslen(text)));
}

void Renderer::draw(HDC target, const RECT& client, const Document& document, const Camera& camera,
                    EditMode mode, const DraftView& draft) const {
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    HBRUSH background = CreateSolidBrush(RGB(15, 18, 26));
    FillRect(dc, &client, background);
    DeleteObject(background);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    const RECT canvas = canvasRect(client);
    const auto projectPoint = [&](const Vec3& point) {
        if (mode == EditMode::Draw2D) return to2DScreen(point, canvas);
        const auto projected = camera.project(point, canvas.right - canvas.left, canvas.bottom - canvas.top);
        return POINT{static_cast<LONG>(projected.x + canvas.left),
                     static_cast<LONG>(projected.y + canvas.top)};
    };
    const int centerX = (canvas.left + canvas.right) / 2;
    const int centerY = (canvas.top + canvas.bottom) / 2;
    HGDIOBJ stockPen = GetCurrentObject(dc, OBJ_PEN);
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(34, 40, 53));
    SelectObject(dc, gridPen);
    if (mode == EditMode::Draw2D) {
        for (int x = centerX % 60; x < canvas.right; x += 60) line(dc, x, canvas.top, x, canvas.bottom);
        for (int y = centerY % 60; y < canvas.bottom; y += 60) line(dc, canvas.left, y, canvas.right, y);
    } else {
        constexpr int gridExtent = 12;
        for (int coordinate = -gridExtent; coordinate <= gridExtent; ++coordinate) {
            const POINT verticalA = projectPoint({static_cast<double>(coordinate), -gridExtent, draft.workPlaneZ});
            const POINT verticalB = projectPoint({static_cast<double>(coordinate), gridExtent, draft.workPlaneZ});
            const POINT horizontalA = projectPoint({-gridExtent, static_cast<double>(coordinate), draft.workPlaneZ});
            const POINT horizontalB = projectPoint({gridExtent, static_cast<double>(coordinate), draft.workPlaneZ});
            line(dc, verticalA.x, verticalA.y, verticalB.x, verticalB.y);
            line(dc, horizontalA.x, horizontalA.y, horizontalB.x, horizontalB.y);
        }
    }
    SelectObject(dc, stockPen);
    DeleteObject(gridPen);

    HPEN axisX = CreatePen(PS_SOLID, 2, RGB(190, 73, 89));
    SelectObject(dc, axisX);
    if (mode == EditMode::Draw2D) line(dc, canvas.left, centerY, canvas.right, centerY);
    else {
        const POINT a = projectPoint({-12.0, 0.0, draft.workPlaneZ});
        const POINT b = projectPoint({12.0, 0.0, draft.workPlaneZ});
        line(dc, a.x, a.y, b.x, b.y);
    }
    SelectObject(dc, stockPen); DeleteObject(axisX);
    HPEN axisY = CreatePen(PS_SOLID, 2, RGB(72, 164, 109));
    SelectObject(dc, axisY);
    if (mode == EditMode::Draw2D) line(dc, centerX, canvas.top, centerX, canvas.bottom);
    else {
        const POINT a = projectPoint({0.0, -12.0, draft.workPlaneZ});
        const POINT b = projectPoint({0.0, 12.0, draft.workPlaneZ});
        line(dc, a.x, a.y, b.x, b.y);
    }
    SelectObject(dc, stockPen); DeleteObject(axisY);

    HPEN modelPen = CreatePen(PS_SOLID, 2, RGB(104, 202, 255));
    SelectObject(dc, modelPen);
    for (const auto& model : document.models()) {
        for (const auto& edge : model.edges()) {
            const POINT from = projectPoint(model.vertices()[edge.from]);
            const POINT to = projectPoint(model.vertices()[edge.to]);
            line(dc, from.x, from.y, to.x, to.y);
        }
    }
    SelectObject(dc, stockPen);
    DeleteObject(modelPen);

    if (mode == EditMode::View3D) drawViewCube(dc, width, draft.cursorScreen);

    const bool drafting = mode == EditMode::Draw2D || draft.drawingActive;
    if (drafting && draft.anchor && draft.cursor) {
        const POINT a = projectPoint(*draft.anchor);
        const POINT b = projectPoint(*draft.cursor);
        HPEN preview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
        SelectObject(dc, preview);
        const auto drawPreviewModel = [&](const WireframeModel& model) {
            for (const auto& edge : model.edges()) {
                const POINT from = projectPoint(model.vertices()[edge.from]);
                const POINT to = projectPoint(model.vertices()[edge.to]);
                line(dc, from.x, from.y, to.x, to.y);
            }
        };
        if (draft.tool == DrawTool::Rectangle) {
            drawPreviewModel(WireframeModel::rectangle(*draft.anchor, *draft.cursor));
        } else if (draft.tool == DrawTool::Circle) {
            const double radius = std::hypot(draft.cursor->x - draft.anchor->x,
                                             draft.cursor->y - draft.anchor->y);
            if (radius > 0.0) drawPreviewModel(WireframeModel::circle(*draft.anchor, radius));
        } else {
            line(dc, a.x, a.y, b.x, b.y);
        }
        SelectObject(dc, stockPen);
        DeleteObject(preview);
    }

    if (drafting && draft.cursor && draft.snapType != SnapType::None) {
        const POINT p = projectPoint(*draft.cursor);
        HPEN snapPen = CreatePen(PS_SOLID, 2, RGB(90, 255, 145));
        SelectObject(dc, snapPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        if (draft.snapType == SnapType::Midpoint) {
            POINT triangle[4]{{p.x, p.y - 7}, {p.x - 7, p.y + 6}, {p.x + 7, p.y + 6}, {p.x, p.y - 7}};
            Polyline(dc, triangle, 4);
        } else {
            Rectangle(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, stockPen);
        DeleteObject(snapPen);
        drawText(dc, p.x + 10, p.y + 8, snapTypeLabel(draft.snapType), RGB(90, 255, 145));
    }

    if (drafting && draft.dynamicInputEnabled && draft.cursor) {
        wchar_t info[128]{};
        if (!draft.input.empty()) {
            std::swprintf(info, std::size(info), L"> %ls", draft.input.c_str());
        } else if (draft.anchor) {
            const double dx = draft.cursor->x - draft.anchor->x;
            const double dy = draft.cursor->y - draft.anchor->y;
            const double dz = draft.cursor->z - draft.anchor->z;
            const double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            std::swprintf(info, std::size(info), L"%.3f < %.1f°  dZ %.3f",
                          std::sqrt(dx * dx + dy * dy + dz * dz), angle, dz);
        } else {
            std::swprintf(info, std::size(info), L"X %.3f  Y %.3f  Z %.3f",
                          draft.cursor->x, draft.cursor->y, draft.cursor->z);
        }
        const int boxX = std::clamp(static_cast<int>(draft.cursorScreen.x + 18), static_cast<int>(canvas.left + 4),
                                    std::max(static_cast<int>(canvas.left + 4), static_cast<int>(canvas.right - 190)));
        const int boxY = std::clamp(static_cast<int>(draft.cursorScreen.y + 18), static_cast<int>(canvas.top + 4),
                                    std::max(static_cast<int>(canvas.top + 4), static_cast<int>(canvas.bottom - 30)));
        RECT inputBox{boxX, boxY, boxX + 184, boxY + 25};
        HBRUSH inputBrush = CreateSolidBrush(RGB(35, 43, 58));
        FillRect(dc, &inputBox, inputBrush); DeleteObject(inputBrush);
        FrameRect(dc, &inputBox, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        drawText(dc, boxX + 7, boxY + 4, info, draft.input.empty() ? RGB(221, 228, 241) : RGB(255, 216, 104));
    }


    SelectObject(dc, oldFont);
    DeleteObject(font);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

} // namespace mm
