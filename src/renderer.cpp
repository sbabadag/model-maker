#include "model_maker/renderer.hpp"
#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cwchar>
#include <map>
#include <string>
#include <tuple>

namespace mm {
namespace {
void line(HDC dc, int x1, int y1, int x2, int y2) {
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

COLORREF nativeColor(std::uint32_t rgb) noexcept {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

int lineWeightPixels(int hundredthsMillimeter) noexcept {
    if (hundredthsMillimeter <= 25) return 2;
    if (hundredthsMillimeter <= 50) return 3;
    if (hundredthsMillimeter <= 100) return 5;
    if (hundredthsMillimeter <= 200) return 7;
    return 9;
}

HPEN createEntityPen(const EntityProperties& properties) {
    const COLORREF color = nativeColor(properties.effectiveColor);
    const int width = lineWeightPixels(properties.effectiveLineWeight);
    std::string type = properties.effectiveLineType;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (type == "CONTINUOUS" || type == "BYLAYER" || type == "BYBLOCK")
        return CreatePen(PS_SOLID, width, color);
    const double scale = std::clamp(properties.lineTypeScale, 0.1, 100.0);
    std::vector<DWORD> pattern;
    const auto scaled = [&](double value) {
        return static_cast<DWORD>(std::max(1.0, std::round(value * scale)));
    };
    if (type.find("CENTER") != std::string::npos) pattern = {scaled(12), scaled(4), scaled(2), scaled(4)};
    else if (type.find("DOT") != std::string::npos) pattern = {scaled(1), scaled(3)};
    else pattern = {scaled(8), scaled(4)};
    LOGBRUSH brush{BS_SOLID, color, 0};
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT | PS_JOIN_ROUND,
                            width, &brush, static_cast<DWORD>(pattern.size()), pattern.data());
    return pen ? pen : CreatePen(PS_SOLID, width, color);
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

void drawViewCube(HDC dc, int viewportWidth, const POINT& cursor, const Camera& camera) {
    const auto cube = ViewCube::layout(viewportWidth, camera);
    const auto hovered = ViewCube::hitTest(cursor.x, cursor.y, viewportWidth, camera);

    RECT panel{cube.centerX - 76, 48, cube.centerX + 76, 238};
    HBRUSH panelBrush = CreateSolidBrush(RGB(20, 26, 37));
    HGDIOBJ oldBrush = SelectObject(dc, panelBrush);
    HPEN panelPen = CreatePen(PS_SOLID, 1, RGB(75, 88, 108));
    HGDIOBJ oldPen = SelectObject(dc, panelPen);
    RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 12, 12);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(panelPen);
    DeleteObject(panelBrush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(244, 247, 252));
    const auto label = [](StandardView view) {
        switch (view) {
        case StandardView::Top: return L"ÜST";
        case StandardView::Bottom: return L"ALT";
        case StandardView::Front: return L"ÖN";
        case StandardView::Back: return L"ARKA";
        case StandardView::Left: return L"SOL";
        case StandardView::Right: return L"SAĞ";
        default: return L"";
        }
    };

    std::array<const ViewCubeFace*, 6> visible{};
    std::size_t visibleCount = 0;
    for (const auto& face : cube.faces) if (face.visible) visible[visibleCount++] = &face;
    for (std::size_t i = 0; i < visibleCount; ++i) {
        for (std::size_t j = i + 1; j < visibleCount; ++j) {
            if (visible[j]->depth < visible[i]->depth) std::swap(visible[i], visible[j]);
        }
    }
    for (std::size_t i = 0; i < visibleCount; ++i) {
        const auto& face = *visible[i];
        const COLORREF normal = face.view == StandardView::Top || face.view == StandardView::Bottom
            ? RGB(112, 133, 155)
            : (face.view == StandardView::Front || face.view == StandardView::Back
                ? RGB(72, 92, 116) : RGB(55, 73, 96));
        polygon(dc, face.points, hovered == face.view ? RGB(42, 155, 220) : normal,
                RGB(210, 222, 235));
    }
    // Paint labels after all faces so an adjacent face never clips the text.
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
        for (const auto& point : face.points) { centerX += point.x; centerY += point.y; }
        centerX /= 4; centerY /= 4;
        RECT text{centerX - 24, centerY - 10, centerX + 24, centerY + 10};
        DrawTextW(dc, label(face.view), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

    RECT home{cube.homeControl.left, cube.homeControl.top, cube.homeControl.right, cube.homeControl.bottom};
    HBRUSH homeBrush = CreateSolidBrush(hovered == StandardView::Isometric
                                        ? RGB(42, 155, 220) : RGB(74, 86, 105));
    FillRect(dc, &home, homeBrush);
    DeleteObject(homeBrush);
    FrameRect(dc, &home, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    DrawTextW(dc, L"ISO", -1, &home, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
}

RECT Renderer::canvasRect(const RECT& client) noexcept {
    return client;
}

Renderer::~Renderer() {
    if (!backBufferDc_) return;
    if (backBufferBitmap_) {
        SelectObject(backBufferDc_, backBufferDefaultBitmap_);
        DeleteObject(backBufferBitmap_);
    }
    DeleteDC(backBufferDc_);
}

HDC Renderer::ensureBackBuffer(HDC target, int width, int height) const {
    if (!backBufferDc_) {
        backBufferDc_ = CreateCompatibleDC(target);
        if (!backBufferDc_) return nullptr;
        backBufferDefaultBitmap_ = GetCurrentObject(backBufferDc_, OBJ_BITMAP);
    }
    if (backBufferBitmap_ && width == backBufferWidth_ && height == backBufferHeight_)
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

bool Renderer::presentRasterZoom(HDC target, int width, int height, Vec2 offset, double factor) const {
    if (!backBufferBitmap_ || backBufferWidth_ != width || backBufferHeight_ != height || factor <= 0.0)
        return false;
    const int destinationX = static_cast<int>(std::lround(offset.x));
    const int destinationY = static_cast<int>(std::lround(offset.y));
    const int destinationWidth = std::max(1, static_cast<int>(std::lround(width * factor)));
    const int destinationHeight = std::max(1, static_cast<int>(std::lround(height * factor)));
    HBRUSH background = CreateSolidBrush(RGB(15, 18, 26));
    if (destinationX > 0) {
        RECT border{0, 0, std::min(width, destinationX), height}; FillRect(target, &border, background);
    }
    if (destinationY > 0) {
        RECT border{0, 0, width, std::min(height, destinationY)}; FillRect(target, &border, background);
    }
    if (destinationX + destinationWidth < width) {
        RECT border{std::max(0, destinationX + destinationWidth), 0, width, height}; FillRect(target, &border, background);
    }
    if (destinationY + destinationHeight < height) {
        RECT border{0, std::max(0, destinationY + destinationHeight), width, height}; FillRect(target, &border, background);
    }
    DeleteObject(background);
    SetStretchBltMode(target, COLORONCOLOR);
    StretchBlt(target, destinationX, destinationY, destinationWidth, destinationHeight,
               backBufferDc_, 0, 0, width, height, SRCCOPY);
    return true;
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
    if (draft.rasterZoomPreview &&
        presentRasterZoom(target, width, height, draft.rasterZoomOffset, draft.rasterZoomFactor)) return;
    HDC dc = ensureBackBuffer(target, width, height);
    if (!dc) return;

    HBRUSH background = CreateSolidBrush(RGB(15, 18, 26));
    FillRect(dc, &client, background);
    DeleteObject(background);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    const RECT canvas = canvasRect(client);
    const auto projectPoint = [&](const Vec3& point) {
        const auto projected = mode == EditMode::Draw2D
            ? camera.project2D(point, canvas.right - canvas.left, canvas.bottom - canvas.top)
            : camera.project(point, canvas.right - canvas.left, canvas.bottom - canvas.top);
        return POINT{static_cast<LONG>(projected.x + canvas.left),
                     static_cast<LONG>(projected.y + canvas.top)};
    };
    const POINT projectedOrigin = projectPoint({0.0, 0.0, 0.0});
    HGDIOBJ stockPen = GetCurrentObject(dc, OBJ_PEN);
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(34, 40, 53));
    SelectObject(dc, gridPen);
    if (mode == EditMode::Draw2D) {
        const POINT origin = projectPoint({0.0, 0.0, 0.0});
        const POINT unit = projectPoint({1.0, 0.0, 0.0});
        const int gridSpacing = std::max(1, static_cast<int>(std::abs(unit.x - origin.x)));
        for (int x = origin.x % gridSpacing; x < canvas.right; x += gridSpacing)
            line(dc, x, canvas.top, x, canvas.bottom);
        for (int y = origin.y % gridSpacing; y < canvas.bottom; y += gridSpacing)
            line(dc, canvas.left, y, canvas.right, y);
    } else {
        constexpr int gridExtent = 10;
        for (int coordinate = -gridExtent; coordinate <= gridExtent; ++coordinate) {
            const POINT verticalA = projectPoint(draft.workPlane.fromPlane({static_cast<double>(coordinate), -gridExtent}));
            const POINT verticalB = projectPoint(draft.workPlane.fromPlane({static_cast<double>(coordinate), gridExtent}));
            const POINT horizontalA = projectPoint(draft.workPlane.fromPlane({-gridExtent, static_cast<double>(coordinate)}));
            const POINT horizontalB = projectPoint(draft.workPlane.fromPlane({gridExtent, static_cast<double>(coordinate)}));
            line(dc, verticalA.x, verticalA.y, verticalB.x, verticalB.y);
            line(dc, horizontalA.x, horizontalA.y, horizontalB.x, horizontalB.y);
        }
    }
    SelectObject(dc, stockPen);
    DeleteObject(gridPen);

    HPEN axisX = CreatePen(PS_SOLID, 2, RGB(190, 73, 89));
    SelectObject(dc, axisX);
    if (mode == EditMode::Draw2D) line(dc, canvas.left, projectedOrigin.y, canvas.right, projectedOrigin.y);
    else {
        const POINT a = projectPoint(draft.workPlane.fromPlane({-10.0, 0.0}));
        const POINT b = projectPoint(draft.workPlane.fromPlane({10.0, 0.0}));
        line(dc, a.x, a.y, b.x, b.y);
    }
    SelectObject(dc, stockPen); DeleteObject(axisX);
    HPEN axisY = CreatePen(PS_SOLID, 2, RGB(72, 164, 109));
    SelectObject(dc, axisY);
    if (mode == EditMode::Draw2D) line(dc, projectedOrigin.x, canvas.top, projectedOrigin.x, canvas.bottom);
    else {
        const POINT a = projectPoint(draft.workPlane.fromPlane({0.0, -10.0}));
        const POINT b = projectPoint(draft.workPlane.fromPlane({0.0, 10.0}));
        line(dc, a.x, a.y, b.x, b.y);
    }
    SelectObject(dc, stockPen); DeleteObject(axisY);

    if (mode == EditMode::View3D) {
        const POINT corners[5]{projectPoint(draft.workPlane.fromPlane({-10.0, -10.0})),
                               projectPoint(draft.workPlane.fromPlane({10.0, -10.0})),
                               projectPoint(draft.workPlane.fromPlane({10.0, 10.0})),
                               projectPoint(draft.workPlane.fromPlane({-10.0, 10.0})),
                               projectPoint(draft.workPlane.fromPlane({-10.0, -10.0}))};
        HPEN planeBorder = CreatePen(PS_DASH, 2, RGB(71, 190, 210));
        SelectObject(dc, planeBorder);
        Polyline(dc, corners, 5);
        SelectObject(dc, stockPen);
        DeleteObject(planeBorder);
        const POINT labelPoint = projectPoint(draft.workPlane.origin);
        drawText(dc, labelPoint.x + 8, labelPoint.y + 8, L"WORK PLANE", RGB(71, 190, 210));
    }

    std::vector<POINT> projectedChain;
    std::size_t interactiveModelStride = 1;
    const auto drawModel = [&](const WireframeModel& model, const Vec3& offset = Vec3{}) {
        const auto& vertices = model.vertices();
        const auto& edges = model.edges();
        const std::size_t stride = draft.interactiveNavigation
            ? std::max<std::size_t>(1, (vertices.size() + 120'000 - 1) / 120'000) : 1;
        bool chain = vertices.size() >= 2 &&
                     (edges.size() == vertices.size() - 1 || edges.size() == vertices.size());
        if (chain) {
            for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
                if (edges[i] != Edge{i, i + 1}) { chain = false; break; }
            }
            if (chain && edges.size() == vertices.size() &&
                edges.back() != Edge{vertices.size() - 1, 0}) chain = false;
        }
        if (chain) {
            projectedChain.clear();
            projectedChain.reserve(vertices.size() / stride + 2);
            for (std::size_t index = 0; index < vertices.size(); index += stride)
                projectedChain.push_back(projectPoint(vertices[index] + offset));
            if ((vertices.size() - 1) % stride != 0)
                projectedChain.push_back(projectPoint(vertices.back() + offset));
            if (edges.size() == vertices.size()) projectedChain.push_back(projectedChain.front());
            constexpr std::size_t chunkSize = 8192;
            for (std::size_t begin = 0; begin + 1 < projectedChain.size(); begin += chunkSize - 1) {
                const std::size_t count = std::min(chunkSize, projectedChain.size() - begin);
                Polyline(dc, projectedChain.data() + begin, static_cast<int>(count));
                if (begin + count == projectedChain.size()) break;
            }
            return;
        }
        for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); edgeIndex += stride) {
            const auto& edge = edges[edgeIndex];
            const POINT from = projectPoint(vertices[edge.from] + offset);
            const POINT to = projectPoint(vertices[edge.to] + offset);
            line(dc, from.x, from.y, to.x, to.y);
        }
    };
    const auto isSelected = [&](std::size_t index) {
        return std::find(draft.selectedModels.begin(), draft.selectedModels.end(), index) != draft.selectedModels.end();
    };

    std::vector<std::size_t> visibleModels;
    if (mode == EditMode::Draw2D) {
        const Vec3 topLeft = camera.unproject2D({0.0, 0.0}, width, height);
        const Vec3 bottomRight = camera.unproject2D({static_cast<double>(width),
                                                     static_cast<double>(height)}, width, height);
        visibleModels = document.query2D(topLeft, bottomRight);
    } else {
        visibleModels.resize(document.models().size());
        for (std::size_t i = 0; i < visibleModels.size(); ++i) visibleModels[i] = i;
    }

    if (draft.interactiveNavigation) {
        constexpr std::size_t interactiveModelBudget = 1'500;
        interactiveModelStride = std::max<std::size_t>(1,
            (document.models().size() + interactiveModelBudget - 1) / interactiveModelBudget);
    }
    constexpr int coverageTileSize = 24;
    const int coverageColumns = (width + coverageTileSize - 1) / coverageTileSize;
    const int coverageRows = (height + coverageTileSize - 1) / coverageTileSize;
    std::vector<bool> interactiveCoverage(static_cast<std::size_t>(coverageColumns * coverageRows));
    std::vector<int> interactiveTiles(document.models().size(), -1);
    std::size_t onScreenSmallModels = 0;
    if (draft.interactiveNavigation) {
        for (const auto index : visibleModels) {
            if (index >= document.models().size()) continue;
            const auto& vertices = document.models()[index].vertices();
            if (vertices.empty() || vertices.size() >= 1'000) continue;
            Vec3 minimum = vertices.front();
            Vec3 maximum = minimum;
            for (const auto& vertex : vertices) {
                minimum.x = std::min(minimum.x, vertex.x); minimum.y = std::min(minimum.y, vertex.y);
                minimum.z = std::min(minimum.z, vertex.z); maximum.x = std::max(maximum.x, vertex.x);
                maximum.y = std::max(maximum.y, vertex.y); maximum.z = std::max(maximum.z, vertex.z);
            }
            const POINT center = projectPoint((minimum + maximum) * 0.5);
            if (center.x >= 0 && center.y >= 0 && center.x < width && center.y < height) {
                interactiveTiles[index] = (center.y / coverageTileSize) * coverageColumns +
                                          center.x / coverageTileSize;
                ++onScreenSmallModels;
            }
        }
    }
    constexpr std::size_t completeOnScreenBudget = 8'000;
    const bool renderAllOnScreen = onScreenSmallModels <= completeOnScreenBudget ||
                                   onScreenSmallModels * 4 < document.models().size() * 3;

    using PenKey = std::tuple<std::uint32_t, int, std::string, int>;
    std::map<PenKey, HPEN> entityPens;
    const auto entityPen = [&](const EntityProperties& properties) {
        const PenKey key{properties.effectiveColor, properties.effectiveLineWeight,
                         properties.effectiveLineType,
                         static_cast<int>(std::round(properties.lineTypeScale * 1000.0))};
        if (const auto found = entityPens.find(key); found != entityPens.end()) return found->second;
        HPEN pen = createEntityPen(properties);
        entityPens.emplace(key, pen);
        return pen;
    };
    for (std::size_t visibleIndex = 0; visibleIndex < visibleModels.size(); ++visibleIndex) {
        const auto index = visibleModels[visibleIndex];
        if (index >= document.models().size() || isSelected(index)) continue;
        const auto& model = document.models()[index];
        if (!model.properties().visible) continue;
        if (draft.interactiveNavigation) {
            bool representative = index % interactiveModelStride == 0 || model.vertices().size() >= 1'000;
            if (interactiveTiles[index] >= 0) {
                const std::size_t tile = static_cast<std::size_t>(interactiveTiles[index]);
                if (renderAllOnScreen || !interactiveCoverage[tile])
                    representative = true;
                if (representative) interactiveCoverage[tile] = true;
            }
            if (!representative) continue;
        }
        SelectObject(dc, entityPen(model.properties()));
        drawModel(model);
    }
    SelectObject(dc, stockPen);
    for (const auto& [key, pen] : entityPens) {
        (void)key;
        DeleteObject(pen);
    }

    if (!draft.selectedModels.empty()) {
        HPEN selectedPen = CreatePen(PS_SOLID, 3, RGB(90, 255, 145));
        SelectObject(dc, selectedPen);
        for (const auto index : draft.selectedModels) {
            if (index < document.models().size()) drawModel(document.models()[index]);
        }
        SelectObject(dc, stockPen);
        DeleteObject(selectedPen);
    }

    if (draft.transformCommand != TransformCommand::None &&
        draft.transformPhase == TransformPhase::Selecting && draft.selectionFirstCorner) {
        const POINT first = *draft.selectionFirstCorner;
        const POINT second = draft.cursorScreen;
        const bool crossing = second.x < first.x;
        RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                       std::max(first.x, second.x), std::max(first.y, second.y)};
        const COLORREF color = crossing ? RGB(74, 188, 112) : RGB(54, 142, 224);
        const int selectionWidth = std::max(1L, selection.right - selection.left);
        const int selectionHeight = std::max(1L, selection.bottom - selection.top);
        HDC overlayDc = CreateCompatibleDC(dc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
        HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
        RECT overlayRect{0, 0, selectionWidth, selectionHeight};
        HBRUSH overlayBrush = CreateSolidBrush(crossing ? RGB(34, 139, 74) : RGB(36, 104, 181));
        FillRect(overlayDc, &overlayRect, overlayBrush);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 72, 0};
        AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                   overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
        SelectObject(overlayDc, oldOverlayBitmap);
        DeleteObject(overlayBrush);
        DeleteObject(overlayBitmap);
        DeleteDC(overlayDc);
        HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN border = CreatePen(crossing ? PS_DASH : PS_SOLID, 1, color);
        HGDIOBJ oldSelectionPen = SelectObject(dc, border);
        Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
        SelectObject(dc, oldSelectionPen);
        SelectObject(dc, oldSelectionBrush);
        DeleteObject(border);
        drawText(dc, selection.left + 3, std::max(3L, selection.top - 19),
                 crossing ? L"CROSSING" : L"WINDOW", color);
    }

    if (draft.zoomWindowActive && draft.zoomWindowFirstCorner) {
        const POINT first = *draft.zoomWindowFirstCorner;
        const POINT second = draft.cursorScreen;
        RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                       std::max(first.x, second.x), std::max(first.y, second.y)};
        const int selectionWidth = std::max(1L, selection.right - selection.left);
        const int selectionHeight = std::max(1L, selection.bottom - selection.top);
        HDC overlayDc = CreateCompatibleDC(dc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
        HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
        RECT overlayRect{0, 0, selectionWidth, selectionHeight};
        HBRUSH overlayBrush = CreateSolidBrush(RGB(36, 104, 181));
        FillRect(overlayDc, &overlayRect, overlayBrush);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 64, 0};
        AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                   overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
        SelectObject(overlayDc, oldOverlayBitmap);
        DeleteObject(overlayBrush); DeleteObject(overlayBitmap); DeleteDC(overlayDc);
        HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN border = CreatePen(PS_SOLID, 1, RGB(54, 142, 224));
        HGDIOBJ oldSelectionPen = SelectObject(dc, border);
        Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
        SelectObject(dc, oldSelectionPen); SelectObject(dc, oldSelectionBrush); DeleteObject(border);
        drawText(dc, selection.left + 3, std::max(3L, selection.top - 19),
                 L"ZOOM WINDOW", RGB(54, 142, 224));
    }

    if (draft.transformCommand != TransformCommand::None &&
        draft.transformPhase == TransformPhase::Destination && draft.transformBase && draft.cursor) {
        const POINT basePoint = projectPoint(*draft.transformBase);
        const POINT destinationPoint = projectPoint(*draft.cursor);
        HPEN trackerPen = CreatePen(PS_DOT, 1, RGB(255, 206, 84));
        SelectObject(dc, trackerPen);
        line(dc, basePoint.x, basePoint.y, destinationPoint.x, destinationPoint.y);
        SelectObject(dc, stockPen);
        DeleteObject(trackerPen);

        const Vec3 displacement = *draft.cursor - *draft.transformBase;
        HPEN transformPreview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
        SelectObject(dc, transformPreview);
        for (const auto index : draft.selectedModels) {
            if (index < document.models().size()) drawModel(document.models()[index], displacement);
        }
        SelectObject(dc, stockPen);
        DeleteObject(transformPreview);
    }

    if (draft.workPlanePicking) {
        HPEN pickPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
        SelectObject(dc, pickPen);
        for (std::size_t index = 1; index < draft.workPlanePoints.size(); ++index) {
            const POINT a = projectPoint(draft.workPlanePoints[index - 1]);
            const POINT b = projectPoint(draft.workPlanePoints[index]);
            line(dc, a.x, a.y, b.x, b.y);
        }
        if (!draft.workPlanePoints.empty() && draft.cursor) {
            const POINT a = projectPoint(draft.workPlanePoints.back());
            const POINT b = projectPoint(*draft.cursor);
            line(dc, a.x, a.y, b.x, b.y);
        }
        SelectObject(dc, stockPen);
        DeleteObject(pickPen);
        for (std::size_t index = 0; index < draft.workPlanePoints.size(); ++index) {
            const POINT point = projectPoint(draft.workPlanePoints[index]);
            HBRUSH marker = CreateSolidBrush(RGB(255, 206, 84));
            HGDIOBJ oldMarker = SelectObject(dc, marker);
            Ellipse(dc, point.x - 5, point.y - 5, point.x + 6, point.y + 6);
            SelectObject(dc, oldMarker);
            DeleteObject(marker);
            const std::wstring label = std::to_wstring(index + 1);
            drawText(dc, point.x + 8, point.y - 14, label.c_str(), RGB(255, 206, 84));
        }
    }

    if (mode == EditMode::View3D) drawViewCube(dc, width, draft.cursorScreen, camera);

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
            if (mode == EditMode::View3D)
                drawPreviewModel(WireframeModel::rectangleOnPlane(draft.workPlane,
                    draft.workPlane.toPlane(*draft.anchor), draft.workPlane.toPlane(*draft.cursor)));
            else drawPreviewModel(WireframeModel::rectangle(*draft.anchor, *draft.cursor));
        } else if (draft.tool == DrawTool::Circle) {
            double radius{};
            if (mode == EditMode::View3D) {
                const Vec2 center = draft.workPlane.toPlane(*draft.anchor);
                const Vec2 edge = draft.workPlane.toPlane(*draft.cursor);
                radius = std::hypot(edge.x - center.x, edge.y - center.y);
                if (radius > 0.0)
                    drawPreviewModel(WireframeModel::circleOnPlane(draft.workPlane, center, radius));
            } else {
                radius = std::hypot(draft.cursor->x - draft.anchor->x,
                                    draft.cursor->y - draft.anchor->y);
                if (radius > 0.0) drawPreviewModel(WireframeModel::circle(*draft.anchor, radius));
            }
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
        } else if (draft.snapType == SnapType::Center || draft.snapType == SnapType::GeometricCenter) {
            Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
            line(dc, p.x - 4, p.y, p.x + 5, p.y); line(dc, p.x, p.y - 4, p.x, p.y + 5);
        } else if (draft.snapType == SnapType::Quadrant) {
            POINT diamond[5]{{p.x, p.y - 7}, {p.x + 7, p.y}, {p.x, p.y + 7},
                             {p.x - 7, p.y}, {p.x, p.y - 7}};
            Polyline(dc, diamond, 5);
        } else if (draft.snapType == SnapType::Intersection ||
                   draft.snapType == SnapType::ApparentIntersection) {
            line(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            line(dc, p.x - 6, p.y + 6, p.x + 7, p.y - 7);
        } else if (draft.snapType == SnapType::Perpendicular) {
            POINT rightAngle[4]{{p.x - 6, p.y + 6}, {p.x - 6, p.y - 5},
                                {p.x + 5, p.y - 5}, {p.x + 5, p.y + 6}};
            Polyline(dc, rightAngle, 4);
        } else if (draft.snapType == SnapType::Tangent) {
            Ellipse(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            line(dc, p.x - 8, p.y + 7, p.x + 8, p.y - 7);
        } else if (draft.snapType == SnapType::Extension || draft.snapType == SnapType::Parallel) {
            line(dc, p.x - 7, p.y, p.x + 8, p.y);
            line(dc, p.x, p.y - 7, p.x, p.y + 8);
        } else {
            Rectangle(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, stockPen);
        DeleteObject(snapPen);
        drawText(dc, p.x + 10, p.y + 8, snapTypeLabel(draft.snapType), RGB(90, 255, 145));
    }

    if (drafting && draft.dynamicInputEnabled && draft.cursor &&
        !(draft.transformCommand != TransformCommand::None && draft.transformPhase == TransformPhase::Selecting)) {
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
}

} // namespace mm
