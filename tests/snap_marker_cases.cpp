// Template for run_snap_marker_scale.py: production code replaces the @ markers.
// Snapshot of the HISTORICAL (1x) marker geometry is hardcoded here as literals;
// the test derives the expected scale*x geometry from it, so the scale claim is
// verified structurally rather than by trusting the production helper. UI-6a:
// the scale is no longer a compile-time constant (kSnapMarkerScale=2) but a
// runtime `int scale` parameter fed from Application::SnapSettings (default 3 =
// 1.5x of the old 2x). The behaviour test now proves EVERY integer scale draws
// exactly scale*x the 1x base, for every symbol.
#include "model_maker/drafting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using COLORREF = std::uint32_t;
constexpr COLORREF RGB(int r, int g, int b) { return r | (g << 8) | (b << 16); }
constexpr int PS_SOLID = 0, NULL_PEN = 8, NULL_BRUSH = 5;
struct POINT { long x{}, y{}; bool operator==(const POINT&) const = default; };
struct Object { bool brush{}; int width{}; COLORREF color{}; bool stock{}; };
using HGDIOBJ = Object*;
using HPEN = Object*;
using HBRUSH = Object*;
struct Call {
    std::string kind;
    std::vector<POINT> points;
    bool operator==(const Call&) const = default;
};
Object stockPenObject{false, 1, 0, true}, stockBrushObject{true, 0, 0, true},
    nullPenObject{false, 0, 0, true}, nullBrushObject{true, 0, 0, true};
Object* const stockPen = &stockPenObject;
Object* const stockBrush = &stockBrushObject;
struct DC {
    Object* pen{stockPen};
    Object* brush{stockBrush};
    POINT cursor;
    std::vector<Call> calls;
};
using HDC = DC*;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
HGDIOBJ GetStockObject(int id) { return id == NULL_PEN ? &nullPenObject : &nullBrushObject; }
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    auto& slot = object->brush ? dc->brush : dc->pen;
    auto old = slot;
    slot = object;
    return old;
}
void record(HDC dc, const std::string& kind, std::vector<POINT> points) {
    dc->calls.push_back({kind, std::move(points)});
}
void MoveToEx(HDC dc, int x, int y, POINT*) { dc->cursor = {x, y}; }
void LineTo(HDC dc, int x, int y) { record(dc, "line", {dc->cursor, {x, y}}); dc->cursor = {x, y}; }
void Polyline(HDC dc, const POINT* points, int n) { record(dc, "polyline", {points, points + n}); }
void Ellipse(HDC dc, int left, int top, int right, int bottom) {
    record(dc, "ellipse", {{left, top}, {right, bottom}});
}
void Rectangle(HDC dc, int left, int top, int right, int bottom) {
    record(dc, "rect", {{left, top}, {right, bottom}});
}
// @LINE_FUNCTION@
using mm::SnapMarkerSymbol;
// @HELPER_BLOCK@

// --- Historical 1x geometry, straight from the pre-change renderer -------------
Call line1(int x1, int y1, int x2, int y2) { return Call{"line", {{x1, y1}, {x2, y2}}}; }
Call poly1(std::vector<POINT> pts) { return Call{"polyline", std::move(pts)}; }
Call ell1(int l, int t, int r, int b) { return Call{"ellipse", {{l, t}, {r, b}}}; }
Call rect1(int l, int t, int r, int b) { return Call{"rect", {{l, t}, {r, b}}}; }

std::vector<Call> baseGeometry(SnapMarkerSymbol symbol) {
    using S = SnapMarkerSymbol;
    switch (symbol) {
    case S::Triangle: return {poly1({{0, -7}, {-7, 6}, {7, 6}, {0, -7}})};
    case S::Circle: return {ell1(-7, -7, 8, 8)};
    case S::CircleCross: return {ell1(-7, -7, 8, 8), line1(-4, 0, 5, 0), line1(0, -4, 0, 5)};
    case S::CrossedCircle:
        return {ell1(-7, -7, 8, 8), line1(-6, -6, 7, 7), line1(-6, 6, 7, -7)};
    case S::Diamond: return {poly1({{0, -7}, {7, 0}, {0, 7}, {-7, 0}, {0, -7}})};
    case S::Cross: return {line1(-6, -6, 7, 7), line1(-6, 6, 7, -7)};
    case S::BoxedCross: return {rect1(-7, -7, 8, 8), line1(-5, -5, 6, 6), line1(-5, 5, 6, -6)};
    case S::ExtensionLine: return {line1(-9, 0, -5, 0), line1(-2, 0, 3, 0), line1(6, 0, 10, 0)};
    case S::LinkedSquares: return {rect1(-7, -7, 2, 2), rect1(-1, -1, 8, 8)};
    case S::RightAngle:
        return {line1(-7, 7, -7, -5), line1(-7, -5, 6, -5), line1(-2, -5, -2, 1),
                line1(-2, 1, 4, 1)};
    case S::TangentCircle: return {ell1(-6, -6, 7, 7), line1(-8, -7, 9, -7)};
    case S::Hourglass:
        return {poly1({{-7, -6}, {7, -6}, {-7, 6}, {7, 6}, {-7, -6}})};
    case S::ParallelLines: return {line1(-8, 5, 0, -5), line1(0, 5, 8, -5)};
    case S::GridCross: return {line1(-7, 0, 8, 0), line1(0, -7, 0, 8), rect1(-3, -3, 4, 4)};
    case S::Square: return {rect1(-6, -6, 7, 7)};
    case S::None: return {};
    }
    return {};
}

std::vector<Call> scaledGeometry(SnapMarkerSymbol symbol, int factor) {
    std::vector<Call> out = baseGeometry(symbol);
    for (Call& call : out)
        for (POINT& point : call.points) {
            point.x *= factor;
            point.y *= factor;
        }
    return out;
}

const std::vector<SnapMarkerSymbol> kAllSymbols{
    SnapMarkerSymbol::Triangle, SnapMarkerSymbol::Circle, SnapMarkerSymbol::CircleCross,
    SnapMarkerSymbol::CrossedCircle, SnapMarkerSymbol::Diamond, SnapMarkerSymbol::Cross,
    SnapMarkerSymbol::BoxedCross, SnapMarkerSymbol::ExtensionLine, SnapMarkerSymbol::LinkedSquares,
    SnapMarkerSymbol::RightAngle, SnapMarkerSymbol::TangentCircle, SnapMarkerSymbol::Hourglass,
    SnapMarkerSymbol::ParallelLines, SnapMarkerSymbol::GridCross, SnapMarkerSymbol::Square};

std::string render(const std::vector<Call>& calls) {
    std::string text;
    for (const Call& call : calls) {
        text += call.kind + "[";
        for (const POINT& point : call.points)
            text += "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
        text += "]";
    }
    return text;
}

std::vector<Call> capture(SnapMarkerSymbol symbol, int scale) {
    DC dc{};
    drawSnapMarkerShape(&dc, POINT{0, 0}, symbol, scale);
    return dc.calls;
}

// Every symbol must draw the SAME SHAPE as before, scaled by `scale` (any int).
void everySymbolScalesBy(int scale) {
    for (SnapMarkerSymbol symbol : kAllSymbols) {
        const std::vector<Call> actual = capture(symbol, scale);
        const std::vector<Call> expected = scaledGeometry(symbol, scale);
        require(actual.size() == expected.size(),
                "call count changed for " + render(expected) + " -> " + render(actual));
        for (std::size_t i = 0; i < actual.size(); ++i) {
            require(actual[i].kind == expected[i].kind, "primitive changed: " + render(actual));
            require(actual[i].points == expected[i].points,
                    "geometry is not exactly " + std::to_string(scale) + "x: expected " +
                        render({expected[i]}) + " got " + render({actual[i]}));
        }
    }
}

// The scale parameter must measurably change geometry: a scale-ignoring
// regression (always drawing 1x) cannot pass.
void scaleChangeIsObservable() {
    require(capture(SnapMarkerSymbol::Square, 2) == scaledGeometry(SnapMarkerSymbol::Square, 2),
            "scale=2 must match the doubled expectation");
    require(!(capture(SnapMarkerSymbol::Square, 2) == scaledGeometry(SnapMarkerSymbol::Square, 1)),
            "1x geometry must NOT satisfy the scale=2 expectation (test would be vacuous)");
    require(!(capture(SnapMarkerSymbol::Square, 3) == scaledGeometry(SnapMarkerSymbol::Square, 2)),
            "scale=3 must differ from scale=2 (test would be vacuous)");
}

// Each marker's bounding span must be EXACTLY scale*x (independent of the
// per-symbol glyph asymmetry: a right-angle marker is not drawn centred).
std::pair<long, long> span(const std::vector<Call>& calls) {
    long minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool any = false;
    for (const Call& call : calls)
        for (const POINT& point : call.points) {
            if (!any) { minX = maxX = point.x; minY = maxY = point.y; any = true; }
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
    return {maxX - minX, maxY - minY};
}

void everySpanScalesBy(int scale) {
    bool grew = false;
    for (SnapMarkerSymbol symbol : kAllSymbols) {
        const auto base = span(baseGeometry(symbol));
        const auto actual = span(capture(symbol, scale));
        require(actual.first == scale * base.first && actual.second == scale * base.second,
                "span is not exactly " + std::to_string(scale) + "x: base " +
                    std::to_string(base.first) + "x" + std::to_string(base.second) + " -> " +
                    std::to_string(actual.first) + "x" + std::to_string(actual.second));
        if (base.first > 0 || base.second > 0) grew = true;
    }
    require(grew, "no symbol had a measurable span (test would be vacuous)");
}

// At the default scale (3), the marker is still a small decoration — the
// largest base offset is 10 (ExtensionLine), so 10*3 = 30 px is the budget.
void staysASmallDecorationAtDefaultScale() {
    constexpr int kDefaultScale = 3;
    for (SnapMarkerSymbol symbol : kAllSymbols) {
        for (const Call& call : capture(symbol, kDefaultScale))
            for (const POINT& point : call.points)
                require(std::labs(point.x) <= 10 * kDefaultScale &&
                            std::labs(point.y) <= 10 * kDefaultScale,
                        "marker offset out of the 3x budget");
    }
}

int main() {
    int passed = 0, failed = 0;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"scale=1 draws every symbol at the historical 1x size",
         [] { everySymbolScalesBy(1); }},
        {"scale=2 draws every symbol exactly twice as large", [] { everySymbolScalesBy(2); }},
        {"scale=3 (UI-6a default) draws every symbol exactly 3x", [] { everySymbolScalesBy(3); }},
        {"scale=4 draws every symbol exactly 4x", [] { everySymbolScalesBy(4); }},
        {"scale change is observable (1x / 2x / 3x all differ)",
         scaleChangeIsObservable},
        {"every marker span scales exactly (1x..4x)", [] {
             everySpanScalesBy(1); everySpanScalesBy(2); everySpanScalesBy(3); everySpanScalesBy(4);
         }},
        {"markers stay a small decoration at the default 3x scale",
         staysASmallDecorationAtDefaultScale},
    };
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << "Snap marker scale: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
