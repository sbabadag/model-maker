// Template for run_workplane_ghost.py: production code replaces the @ markers.
#include "model_maker/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mm::Vec3;
using mm::Vec2;
using COLORREF = std::uint32_t;
constexpr COLORREF RGB(int r, int g, int b) { return r | (g << 8) | (b << 16); }
constexpr int PS_SOLID = 0, NULL_PEN = 8, NULL_BRUSH = 5, HS_FDIAGONAL = 2, TRANSPARENT = 1;
struct POINT { long x{}, y{}; bool operator==(const POINT&) const = default; };
struct RECT { long left{}, top{}, right{}, bottom{}; };
struct Object { bool brush{}; int width{}; COLORREF color{}; bool stock{}; bool hatch{}; };
using HGDIOBJ = Object*;
using HPEN = Object*;
using HBRUSH = Object*;
struct Call {
    std::string kind;
    std::vector<POINT> points;
    int width{};
    COLORREF color{}, fill{};
    bool hatch{};
};
Object stockPenObject{false, 1, 0, true, false}, stockBrushObject{true, 0, 0, true, false},
    nullPenObject{false, 0, 0, true, false}, nullBrushObject{true, 0, 0, true, false};
// Mirrors the renderer: stockPen/stockBrush are HGDIOBJ values, not objects.
Object* const stockPen = &stockPenObject;
Object* const stockBrush = &stockBrushObject;
struct DC {
    Object* pen{stockPen};
    Object* brush{stockBrush};
    POINT cursor;
    POINT brushOrg;                  // DC brush pattern origin (SetBrushOrgEx)
    std::vector<Call> calls;
    std::vector<POINT> brushOrgSets; // every SetBrushOrgEx call, in order
};
using HDC = DC*;
int liveObjects = 0;
// Sub-pixel camera state, as the real camera actually varies it during pan/zoom:
//  g_pixelOffset = whole/fractional screen translation (pan)
//  g_zoom        = continuously varying projection scale (zoom). THIS is what
//                  makes a rounded-projection scale derivation wobble: the true
//                  scale moves smoothly while the rounded pixel gap steps.
double g_pixelOffset = 0.0;
double g_zoom = 1.0;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
HPEN CreatePen(int, int width, COLORREF color) { ++liveObjects; return new Object{false, width, color}; }
HBRUSH CreateSolidBrush(COLORREF color) { ++liveObjects; return new Object{true, 0, color}; }
HBRUSH CreateHatchBrush(int, COLORREF color) { ++liveObjects; return new Object{true, 0, color, false, true}; }
HGDIOBJ GetStockObject(int id) {
    require(id == NULL_PEN || id == NULL_BRUSH, "unexpected stock object");
    return id == NULL_PEN ? &nullPenObject : &nullBrushObject;
}
int SetBkMode(HDC, int) { return 0; }
int SetBrushOrgEx(HDC dc, int x, int y, POINT* previous) {
    if (previous) *previous = dc->brushOrg;
    dc->brushOrg = {x, y};
    dc->brushOrgSets.push_back({x, y});
    return 1;
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    auto& slot = object->brush ? dc->brush : dc->pen;
    auto old = slot;
    slot = object;
    return old;
}
void DeleteObject(HGDIOBJ object) { require(!object->stock, "stock object deleted"); --liveObjects; delete object; }
void record(HDC dc, const std::string& kind, std::vector<POINT> points) {
    dc->calls.push_back({kind, std::move(points), dc->pen->width, dc->pen->color, dc->brush->color, dc->brush->hatch});
}
void MoveToEx(HDC dc, int x, int y, POINT*) { dc->cursor = {x, y}; }
void LineTo(HDC dc, int x, int y) { record(dc, "line", {dc->cursor, {x, y}}); dc->cursor = {x, y}; }
void Polygon(HDC dc, const POINT* points, int n) { record(dc, "polygon", {points, points + n}); }
// @LINE_FUNCTION@
struct Draft {
    bool workPlaneGhostVisible{true};
    mm::WorkPlane workPlane{};
};
// Deliberately non-axis-aligned screen projection, independent of renderer logic.
// The precise variant is the same affine map without the integer landing, so the
// pair models exactly what renderer.cpp does (POINT for drawing, Vec2 for scale).
POINT projectPoint(Vec3 p) {
    return {std::lround(400 + g_pixelOffset + g_zoom * (65 * p.x - 24 * p.y)),
            std::lround(300 + g_pixelOffset + g_zoom * (65 * p.z - 24 * p.y))};
}
Vec2 projectPointPrecise(Vec3 p) {
    return {400 + g_pixelOffset + g_zoom * (65 * p.x - 24 * p.y),
            300 + g_pixelOffset + g_zoom * (65 * p.z - 24 * p.y)};
}
// Brush-origin calls made by the last render() (SetBrushOrgEx order preserved).
std::vector<POINT> g_brushOrgSets;
std::vector<Call> render(const Draft& draft, int span) {
    DC context;
    HDC targetDc = &context;
    RECT canvas{0, 0, span, span};
    (void)canvas;
// @GHOST_BLOCK@
    require(context.pen == stockPen && context.brush == stockBrush, "GDI selections not restored");
    require(context.brushOrg.x == 0 && context.brushOrg.y == 0,
            "brush origin not restored after the plane fill");
    require(liveObjects == 0, "GDI object leak");
    g_brushOrgSets = context.brushOrgSets;
    return context.calls;
}
// Rotated orthonormal frame about Z: detects a regression to grid/world-axis planes.
mm::WorkPlane rotatedPlane() {
    return mm::WorkPlane{{12, -8, 20}, {0.6, 0.8, 0}, {-0.8, 0.6, 0}, {0, 0, 1}};
}
Draft fixture(const mm::WorkPlane& plane) {
    Draft d;
    d.workPlaneGhostVisible = true;
    d.workPlane = plane;
    return d;
}
std::vector<Call> select(const std::vector<Call>& calls, const std::string& kind) {
    std::vector<Call> result;
    for (const auto& c : calls) if (c.kind == kind) result.push_back(c);
    return result;
}
int channel(COLORREF color, int index) { return (color >> (8 * index)) & 255; }
double luminance(COLORREF color) {
    double values[3];
    for (int i = 0; i < 3; ++i) {
        double c = channel(color, i) / 255.0;
        values[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * values[0] + 0.7152 * values[1] + 0.0722 * values[2];
}
double contrast(COLORREF a, COLORREF b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
double perpendicularity(POINT a, POINT b) {
    const double ax = a.x, ay = a.y, bx = b.x, by = b.y;
    const double na = std::hypot(ax, ay), nb = std::hypot(bx, by);
    if (na < 1e-9 || nb < 1e-9) return 0.0; // degenerate: trivially "parallel"
    return std::abs(ax * by - ay * bx) / (na * nb);
}
bool sameDirection(POINT a, POINT b) { return (a.x * b.x + a.y * b.y) > 0; }

void flagGatesDrawingAndStaysRenderOnly() {
    auto off = fixture(rotatedPlane());
    off.workPlaneGhostVisible = false;
    require(render(off, 800).empty(), "ghost plane must not draw when its flag is off");
    const auto on = render(fixture(rotatedPlane()), 800);
    require(!on.empty(), "ghost plane must draw when its flag is on (default)");
    for (const auto& c : on)
        require(c.kind == "line" || c.kind == "polygon",
                "ghost plane may only emit drawing calls (non-selectable / non-snappable), got " + c.kind);
}

void translucentFillMeshAndBorder() {
    const auto calls = render(fixture(rotatedPlane()), 800);
    const auto polygons = select(calls, "polygon");
    require(polygons.size() == 1, "exactly one plane quad, got " + std::to_string(polygons.size()));
    require(polygons[0].points.size() == 4, "plane quad must have four corners");
    require(polygons[0].hatch, "plane fill must be a screen-door (hatch) - translucent, hides nothing");
    require(polygons[0].fill == RGB(126, 156, 186), "plane fill must use the ghost hatch colour");
    const auto strokes = select(calls, "line");
    require(strokes.size() == 14, "plane needs 4 border + 10 mesh strokes, got " + std::to_string(strokes.size()));
    std::vector<Call> border, mesh;
    for (const auto& c : strokes) (c.width == 2 ? border : mesh).push_back(c);
    require(border.size() == 4, "border must be four strokes (closed frame), got " + std::to_string(border.size()));
    require(mesh.size() == 10, "mesh must be ten strokes (6x6 divisions), got " + std::to_string(mesh.size()));
    for (const auto& c : border) {
        require(c.width == 2, "border stroke must be 2px");
        require(c.color == RGB(74, 110, 152), "border stroke must use the ghost frame colour");
        require(c.points.size() == 2, "each border stroke is one segment");
    }
    for (const auto& c : mesh) {
        require(c.width == 1, "mesh stroke must be 1px");
        require(c.color == RGB(146, 176, 204), "mesh stroke must use the ghost mesh colour");
    }
    // Border segments must chain into a closed loop over the four quad corners.
    require(border[0].points[0] == border[3].points[1], "border frame must close back to its first corner");
    for (int i = 0; i < 3; ++i)
        require(border[i].points[1] == border[i + 1].points[0], "border segments must chain corner to corner");
    std::vector<POINT> corners;
    for (const auto& c : border) corners.push_back(c.points[0]);
    for (const auto& c : polygons[0].points) {
        bool found = false;
        for (const auto& corner : corners) found = found || corner == c;
        require(found, "every quad corner must be a border vertex");
    }
}

void quadFollowsTheWorkPlaneFrame() {
    const auto plane = rotatedPlane();
    const auto calls = render(fixture(plane), 800);
    const auto quad = select(calls, "polygon")[0].points;
    long cx = 0, cy = 0;
    for (const auto& p : quad) { cx += p.x; cy += p.y; }
    const POINT origin = projectPoint(plane.origin);
    require(std::abs(cx / 4 - origin.x) <= 2 && std::abs(cy / 4 - origin.y) <= 2,
            "plane quad must be centred on the work plane origin");
    // Long-baseline U/V directions: a 1-unit baseline is only ~28px here, so
    // integer rounding would swamp the direction. The plane is affine, so a
    // larger baseline gives the same direction with negligible rounding.
    constexpr double kBaseline = 10000.0;
    const POINT alongU{projectPoint(plane.fromPlane({kBaseline, 0.0})).x - projectPoint(plane.fromPlane({-kBaseline, 0.0})).x,
                       projectPoint(plane.fromPlane({kBaseline, 0.0})).y - projectPoint(plane.fromPlane({-kBaseline, 0.0})).y};
    const POINT alongV{projectPoint(plane.fromPlane({0.0, kBaseline})).x - projectPoint(plane.fromPlane({0.0, -kBaseline})).x,
                       projectPoint(plane.fromPlane({0.0, kBaseline})).y - projectPoint(plane.fromPlane({0.0, -kBaseline})).y};
    const POINT edgeU{quad[1].x - quad[0].x, quad[1].y - quad[0].y};
    const POINT edgeV{quad[2].x - quad[1].x, quad[2].y - quad[1].y};
    require(perpendicularity(edgeU, alongV) > 0.05, "fixture must be oblique enough to discriminate orientation");
    // A world-axis regression would put this ~0.85 away from the plane axes.
    require(perpendicularity(edgeU, alongU) < 0.01, "quad edge must run along the work plane U axis");
    require(perpendicularity(edgeV, alongV) < 0.01, "quad edge must run along the work plane V axis");
    require(sameDirection(edgeU, alongU) && sameDirection(edgeV, alongV), "quad edges must follow U/V, not flip");
    // Regression guard: a world-axis plane would put this edge parallel to screen X.
    require(perpendicularity(alongU, POINT{1, 0}) > 0.05, "fixture U is not a screen axis");
}

void remainsScreenSpaceSized() {
    const auto small = select(render(fixture(rotatedPlane()), 400), "polygon")[0].points;
    const auto large = select(render(fixture(rotatedPlane()), 800), "polygon")[0].points;
    const auto span = [](const std::vector<POINT>& quad) {
        long left = quad[0].x, right = quad[0].x, top = quad[0].y, bottom = quad[0].y;
        for (const auto& p : quad) {
            left = std::min(left, p.x); right = std::max(right, p.x);
            top = std::min(top, p.y); bottom = std::max(bottom, p.y);
        }
        return std::pair<long, long>{right - left, bottom - top};
    };
    const auto [sw, sh] = span(small);
    const auto [lw, lh] = span(large);
    require(sw > 0 && sh > 0, "plane quad must have a finite on-screen extent");
    const double ratioW = static_cast<double>(lw) / sw, ratioH = static_cast<double>(lh) / sh;
    require(std::abs(ratioW - 2.0) < 0.05 && std::abs(ratioH - 2.0) < 0.05,
            "doubling the canvas must double the plane: got " + std::to_string(ratioW) + " x " + std::to_string(ratioH));
}

void ghostPaletteIsVisibleButFaint() {
    constexpr COLORREF paleCanvas = 198 | (224 << 8) | (246 << 16);
    const auto calls = render(fixture(rotatedPlane()), 800);
    const auto border = select(calls, "line")[13];
    const auto mesh = select(calls, "line")[0];
    const auto fill = select(calls, "polygon")[0].fill;
    require(contrast(border.color, paleCanvas) >= 3.0, "plane frame must be clearly visible (>=3:1)");
    require(contrast(mesh.color, paleCanvas) >= 1.3 && contrast(mesh.color, paleCanvas) <= 3.0,
            "mesh must stay faint (1.3..3.0)");
    require(contrast(fill, paleCanvas) >= 1.3 && contrast(fill, paleCanvas) <= 3.5,
            "fill hatch must stay faint (1.3..3.5)");
    require(luminance(border.color) < luminance(fill) && luminance(fill) < luminance(mesh.color),
            "frame must be darkest, fill hatch in between, mesh faintest");
}

void survivesAnEdgeOnPlane() {
    // U is chosen parallel to the projection's null direction: the quad collapses.
    const Vec3 nullDirection{0.3273, 0.8865, 0.3273};
    Draft d = fixture(mm::WorkPlane{{0, 0, 0}, nullDirection, {0, 0, 1}, {0, 1, 0}});
    const auto calls = render(d, 800); // must not throw
    require(select(calls, "polygon").size() == 1, "edge-on plane still fills once");
    require(select(calls, "line").size() == 14, "edge-on plane still draws its frame and mesh");
}

// FLICKER KORUMASI: duzlemin EKRAN boyutu kameranin piksel ARASINA dustugu yere
// bagli olmamali. Olcek tamsayiya oturtulmus projeksiyondan turetilirse zoom'da
// boyut kare kare ~%1.5 oynar (buyuk canvas'ta yuzlerce px) ve duzlem titrer.
// Duzeltmede sapma yalnizca kose yuvarlamasindan gelir (<= ~2 px).
void planeScreenSizeIgnoresSubPixelOffset() {
    constexpr int span = 20000; // buyuk span: niceleme gurultusunu bastirir
    const auto quadSpan = [&](double zoom) {
        g_zoom = zoom;
        const auto quad = select(render(fixture(rotatedPlane()), span), "polygon")[0].points;
        long left = quad[0].x, right = quad[0].x, top = quad[0].y, bottom = quad[0].y;
        for (const auto& p : quad) {
            left = std::min(left, p.x); right = std::max(right, p.x);
            top = std::min(top, p.y); bottom = std::max(bottom, p.y);
        }
        return std::pair<long, long>{right - left, bottom - top};
    };
    const auto [w0, h0] = quadSpan(1.0);
    require(w0 > 1000 && h0 > 1000, "fixture must render a large plane");
    // Gercek zoom gibi SUREKLI olcek taramasi (tam sayi sinirlarindan gecer).
    for (const double zoom : {1.003, 1.007, 1.011, 1.015, 1.019, 1.023, 1.027}) {
        const auto [w, h] = quadSpan(zoom);
        require(std::abs(w - w0) <= 3 && std::abs(h - h0) <= 3,
                "plane screen size must not wobble as the camera zooms: " +
                std::to_string(w) + "x" + std::to_string(h) + " vs " +
                std::to_string(w0) + "x" + std::to_string(h0) + " at zoom " +
                std::to_string(zoom));
    }
    g_zoom = 1.0;
}

// Desen fircasi (hatch) cihaz kaynagina cakilirsa duzlem pan/zoom'da kayarken
// doku duzlemin altindan akar (crawling = flicker). Kaynak duzlem orijinine
// faz-kilitli olmali: 8 px'lik tam periyot fazi degistirmez, kismi adim degistirir
// — yani doku duzlemle BIRLIKTE hareket eder.
void brushOriginAnchorsThePatternToThePlane() {
    const auto plane = rotatedPlane();
    const auto phase = [](double value) {
        const long v = std::lround(value) % 8;
        return v < 0 ? v + 8 : v;
    };
    const auto expectedPhase = [&]() {
        const Vec2 origin = projectPointPrecise(plane.origin);
        return POINT{phase(origin.x), phase(origin.y)};
    };
    g_pixelOffset = 0.0;
    (void)render(fixture(plane), 800);
    require(g_brushOrgSets.size() == 2, "plane fill must set and restore the brush origin");
    require(g_brushOrgSets[1].x == 0 && g_brushOrgSets[1].y == 0, "brush origin must be restored");
    const POINT base = expectedPhase();
    require(g_brushOrgSets[0] == base, "brush origin must be phase-locked to the plane origin");
    g_pixelOffset = 8.0; // tam desen periyodu: faz AYNI kalmali
    (void)render(fixture(plane), 800);
    require(g_brushOrgSets[0] == expectedPhase(), "a full 8px camera step must not shift the pattern phase");
    g_pixelOffset = 3.0; // kismi adim: doku duzlemle kaymali
    (void)render(fixture(plane), 800);
    const POINT shifted = expectedPhase();
    require(g_brushOrgSets[0] == shifted, "the pattern must follow the plane, not the device origin");
    require(!(shifted == base), "fixture must actually shift the phase (else the check is vacuous)");
    g_pixelOffset = 0.0;
}

int main() {
    int passed = 0, failed = 0;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"flag gates drawing; ghost plane is render-only", flagGatesDrawingAndStaysRenderOnly},
        {"translucent fill, meshed quad and closed frame", translucentFillMeshAndBorder},
        {"quad follows the work plane frame (local, not world)", quadFollowsTheWorkPlaneFrame},
        {"screen-space sizing survives canvas changes", remainsScreenSpaceSized},
        {"ghost palette is visible yet faint", ghostPaletteIsVisibleButFaint},
        {"edge-on plane degrades without crashing", survivesAnEdgeOnPlane},
        {"plane screen size is immune to sub-pixel camera offset", planeScreenSizeIgnoresSubPixelOffset},
        {"hatch pattern is anchored to the plane (no crawl)", brushOriginAnchorsThePatternToThePlane},
    };
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << "Work plane ghost renderer: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
