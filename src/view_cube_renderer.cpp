#include "model_maker/view_cube_renderer.hpp"

#include "model_maker/renderer.hpp" // canvasBackgroundColor() — tek kaynak
#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace mm {
namespace {

// --- MODERN PALET (Tek kaynak; baska yerde literalm yok) ---------------------
constexpr COLORREF kPanelFill = RGB(24, 31, 44);
constexpr COLORREF kPanelBorder = RGB(96, 112, 138);
constexpr COLORREF kPanelHighlight = RGB(58, 72, 96);
constexpr COLORREF kFaceBorder = RGB(206, 220, 236);
constexpr COLORREF kFaceBorderHot = RGB(168, 226, 255);
constexpr COLORREF kFaceHotFill = RGB(44, 143, 210);
constexpr COLORREF kAccentAmber = RGB(255, 186, 44);
constexpr COLORREF kLabelColor = RGB(238, 244, 252);
constexpr COLORREF kHomeFill = RGB(46, 58, 78);
constexpr COLORREF kHomeFillHot = RGB(44, 143, 210);
constexpr COLORREF kHomeBorder = RGB(122, 140, 168);

COLORREF mixColor(COLORREF a, COLORREF b, double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    const auto blend = [t](int x, int y) {
        return static_cast<int>(std::lround(x + (y - x) * t));
    };
    return RGB(blend(GetRValue(a), GetRValue(b)),
               blend(GetGValue(a), GetGValue(b)),
               blend(GetBValue(a), GetBValue(b)));
}

void line(HDC dc, int x1, int y1, int x2, int y2, int width, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ previous = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, previous);
    DeleteObject(pen);
}

template <std::size_t Size>
void polygon(HDC dc, const std::array<CubePoint, Size>& points, COLORREF fill, COLORREF border,
             int borderWidth) {
    std::array<POINT, Size> native{};
    for (std::size_t i = 0; i < Size; ++i) native[i] = {points[i].x, points[i].y};
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, native.data(), static_cast<int>(native.size()));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Yuz taban rengi: eksene gore hafif ton farki (kup okunurlugu), sonra
// izleyiciye bakanlik (depth) kadar aydinlatilir. Yuzun "on" olmasi onu
// belirgin sekilde one cikarir — modern kup gorunumu.
COLORREF faceFill(const ViewCubeFace& face) noexcept {
    COLORREF base = RGB(58, 74, 100);
    switch (face.view) {
    case StandardView::Top:
    case StandardView::Bottom: base = RGB(96, 118, 148); break;
    case StandardView::Front:
    case StandardView::Back: base = RGB(68, 86, 114); break;
    default: break; // X yuzleri (Sol/Sag)
    }
    return mixColor(base, RGB(176, 202, 232), 0.35 * std::clamp(face.depth, 0.0, 1.0));
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

// Kenar hedefinin iki ucu (kupsel kose -> ekran).
std::array<CubePoint, 2> edgeEndpoints(const ViewCubeTarget& target, const ViewCubeLayout& cube,
                                       const Camera& camera) noexcept {
    const auto endpoint = [&](int signedValue) {
        const Vec3 point{static_cast<double>(target.dx == 0 ? signedValue : target.dx),
                         static_cast<double>(target.dy == 0 ? signedValue : target.dy),
                         static_cast<double>(target.dz == 0 ? signedValue : target.dz)};
        const Vec3 view = camera.viewTransform(point);
        return CubePoint{static_cast<int>(std::lround(cube.centerX + view.x * kViewCubeScalePx)),
                         static_cast<int>(std::lround(cube.centerY - view.y * kViewCubeScalePx))};
    };
    return {endpoint(-1), endpoint(1)};
}

} // namespace

ViewCubeRenderer::~ViewCubeRenderer() {
    if (labelFont_) DeleteObject(labelFont_);
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
                            const Camera& camera) const {
    const int canvasWidth = std::max(1L, client.right - client.left);
    const auto cube = ViewCube::layout(canvasWidth, camera);

    // Panel: kup + ISO dugmesi + XYZ triadi. containsWidget() ile AYNI
    // dikdortgen (tiklanan bolge cizilen bolgeden kaymasin).
    const RECT panel{cube.centerX - 76, 48, cube.centerX + 76, 238};
    const int panelWidth = panel.right - panel.left;
    const int panelHeight = panel.bottom - panel.top;
    if (panelWidth < 16 || panelHeight < 16) return;

    HDC dc = ensureBackBuffer(target, panelWidth, panelHeight);
    if (!dc) return;

    // Panel arka plani TUVAL RENGI: yuvarlatilmis koseler tuvali gostersin
    // (arka tampon kare; aksi halde koseler siyah kalirdi).
    RECT full{0, 0, panelWidth, panelHeight};
    HBRUSH background = CreateSolidBrush(canvasBackgroundColor());
    FillRect(dc, &full, background);
    DeleteObject(background);

    // Cizim TUVAL koordinatinda yapilir; viewport orijini paneli kaydirir.
    POINT previousOrigin{};
    SetViewportOrgEx(dc, -panel.left, -panel.top, &previousOrigin);
    SetBkMode(dc, TRANSPARENT);

    const auto hovered = ViewCube::hitTest(cursor.x, cursor.y, canvasWidth, camera);
    const bool hoverIsFace = hovered && hovered->kind == ViewCubeTargetKind::Face;
    const bool hoverIsEdge = hovered && hovered->kind == ViewCubeTargetKind::Edge;
    const bool hoverIsCorner = hovered && hovered->kind == ViewCubeTargetKind::Corner;
    const bool hoverIsHome = hovered && hovered->kind == ViewCubeTargetKind::Home;

    // --- 1) PANEL ---------------------------------------------------------
    {
        HBRUSH brush = CreateSolidBrush(kPanelFill);
        HPEN pen = CreatePen(PS_SOLID, 1, kPanelBorder);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 14, 14);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        // Ust ic kenar parlamasi (yumusak "cam" hissi).
        line(dc, panel.left + 8, panel.top + 2, panel.right - 8, panel.top + 2, 1, kPanelHighlight);
    }

    // --- 2) KUP YUZLERI (arkadan one) -------------------------------------
    std::array<const ViewCubeFace*, 6> visible{};
    std::size_t visibleCount = 0;
    for (const auto& face : cube.faces)
        if (face.visible) visible[visibleCount++] = &face;
    for (std::size_t i = 0; i < visibleCount; ++i)
        for (std::size_t j = i + 1; j < visibleCount; ++j)
            if (visible[j]->depth < visible[i]->depth) std::swap(visible[i], visible[j]);

    for (std::size_t i = 0; i < visibleCount; ++i) {
        const auto& face = *visible[i];
        const bool hot = hoverIsFace && hovered->face == face.view;
        polygon(dc, face.points, hot ? kFaceHotFill : faceFill(face),
                hot ? kFaceBorderHot : kFaceBorder, hot ? 2 : 1);
    }

    // --- 3) AKTIF HEDEF: KENAR / KOSE -------------------------------------
    if (hoverIsEdge) {
        const auto ends = edgeEndpoints(*hovered, cube, camera);
        line(dc, ends[0].x, ends[0].y, ends[1].x, ends[1].y, 4, kAccentAmber);
    } else if (hoverIsCorner) {
        for (std::size_t i = 0; i < cube.cornerTargets.size(); ++i) {
            if (!(cube.cornerTargets[i] == *hovered)) continue;
            const auto& point = cube.corners[i];
            HBRUSH brush = CreateSolidBrush(kAccentAmber);
            HPEN pen = CreatePen(PS_SOLID, 2, kFaceBorderHot);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            Ellipse(dc, point.x - 7, point.y - 7, point.x + 7, point.y + 7);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
            DeleteObject(brush);
            break;
        }
    }

    // --- 4) YUZ ETIKETLERI ------------------------------------------------
    if (!labelFont_) {
        labelFont_ = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    if (labelFont_) {
        HGDIOBJ previousFont = SelectObject(dc, labelFont_);
        SetTextColor(dc, kLabelColor);
        for (std::size_t i = 0; i < visibleCount; ++i) {
            const auto& face = *visible[i];
            long long twiceArea = 0;
            for (std::size_t point = 0; point < face.points.size(); ++point) {
                const auto& a = face.points[point];
                const auto& b = face.points[(point + 1) % face.points.size()];
                twiceArea += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
            }
            if (std::abs(twiceArea) < 900) continue; // yandan bakilan yuz: yazi okunmaz
            int centerX = 0;
            int centerY = 0;
            for (const auto& point : face.points) {
                centerX += point.x;
                centerY += point.y;
            }
            centerX /= static_cast<int>(face.points.size());
            centerY /= static_cast<int>(face.points.size());
            RECT text{centerX - 26, centerY - 9, centerX + 26, centerY + 9};
            DrawTextW(dc, faceLabel(face.view), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, previousFont);
    }

    // --- 5) XYZ TRIADI ----------------------------------------------------
    {
        HGDIOBJ previousFont = labelFont_ ? SelectObject(dc, labelFont_) : nullptr;
        const auto axis = [&](const CubePoint& end, COLORREF color, const wchar_t* name) {
            line(dc, cube.axisOrigin.x, cube.axisOrigin.y, end.x, end.y, 2, color);
            SetTextColor(dc, color);
            RECT text{end.x - 8, end.y - 9, end.x + 8, end.y + 9};
            DrawTextW(dc, name, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };
        axis(cube.xAxis, RGB(235, 82, 96), L"X");
        axis(cube.yAxis, RGB(72, 211, 121), L"Y");
        axis(cube.zAxis, RGB(78, 148, 255), L"Z");
        if (previousFont) SelectObject(dc, previousFont);
    }

    // --- 6) ISO / EV DUGMESI ---------------------------------------------
    {
        RECT home{cube.homeControl.left, cube.homeControl.top,
                  cube.homeControl.right, cube.homeControl.bottom};
        HBRUSH brush = CreateSolidBrush(hoverIsHome ? kHomeFillHot : kHomeFill);
        HPEN pen = CreatePen(PS_SOLID, 1, hoverIsHome ? kFaceBorderHot : kHomeBorder);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, home.left, home.top, home.right, home.bottom, 8, 8);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        if (labelFont_) {
            HGDIOBJ previousFont = SelectObject(dc, labelFont_);
            SetTextColor(dc, kLabelColor);
            DrawTextW(dc, L"ISO", -1, &home, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, previousFont);
        }
    }

    SetViewportOrgEx(dc, previousOrigin.x, previousOrigin.y, nullptr);
    BitBlt(target, panel.left, panel.top, panelWidth, panelHeight, dc, 0, 0, SRCCOPY);
}

} // namespace mm
