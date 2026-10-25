// Template for run_gumball_render.py: production code replaces the @ markers.
#include "model_maker/geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mm::Vec3;
using COLORREF = std::uint32_t;
constexpr COLORREF RGB(int r, int g, int b) { return r | (g << 8) | (b << 16); }
constexpr int PS_SOLID = 0, NULL_BRUSH = 5;
struct POINT { long x{}, y{}; bool operator==(const POINT&) const = default; };
struct Object { bool brush{}; int width{}; COLORREF color{}; bool stock{}; };
using HGDIOBJ = Object*;
using HPEN = Object*;
using HBRUSH = Object*;
struct Call {
    std::string kind;
    std::vector<POINT> points;
    int width{};
    COLORREF color{}, fill{};
};
Object stockPen{false, 1, 0, true}, stockBrush{true, 0, 0, true}, nullBrush{true, 0, 0, true};
struct DC { Object* pen{&stockPen}; Object* brush{&stockBrush}; POINT cursor; std::vector<Call> calls; };
using HDC = DC*;
int liveObjects = 0;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
HPEN CreatePen(int, int width, COLORREF color) { ++liveObjects; return new Object{false, width, color}; }
HBRUSH CreateSolidBrush(COLORREF color) { ++liveObjects; return new Object{true, 0, color}; }
HGDIOBJ GetStockObject(int id) { require(id == NULL_BRUSH, "unexpected stock object"); return &nullBrush; }
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    auto& slot = object->brush ? dc->brush : dc->pen;
    auto old = slot;
    slot = object;
    return old;
}
void DeleteObject(HGDIOBJ object) { require(!object->stock, "stock object deleted"); --liveObjects; delete object; }
void record(HDC dc, const std::string& kind, std::vector<POINT> points) {
    dc->calls.push_back({kind, std::move(points), dc->pen->width, dc->pen->color, dc->brush->color});
}
void MoveToEx(HDC dc, int x, int y, POINT*) { dc->cursor = {x, y}; }
void LineTo(HDC dc, int x, int y) { record(dc, "line", {dc->cursor, {x, y}}); dc->cursor = {x, y}; }
void Polygon(HDC dc, const POINT* points, int n) { record(dc, "polygon", {points, points + n}); }
void Polyline(HDC dc, const POINT* points, int n) { record(dc, "polyline", {points, points + n}); }
void Rectangle(HDC dc, int x1, int y1, int x2, int y2) { record(dc, "rectangle", {{x1, y1}, {x2, y2}}); }
void Ellipse(HDC dc, int x1, int y1, int x2, int y2) { record(dc, "ellipse", {{x1, y1}, {x2, y2}}); }
// @LINE_FUNCTION@
// @GUMBALL_HANDLES@
struct Draft {
    bool interactiveNavigation{};
// @GUMBALL_FIELDS@
};
// Deliberately non-axis-aligned screen projection, independent of renderer logic.
POINT projectPoint(Vec3 p) {
    return {std::lround(350 + p.x + 0.35 * p.y - 0.4 * p.z),
            std::lround(300 + 0.2 * p.x - p.y - 0.7 * p.z)};
}
std::vector<Call> render(const Draft& draft) {
    DC context;
    HDC dc = &context;
    struct { int drawCalls{}; } performance;
// @GUMBALL_BLOCK@
    require(context.pen == &stockPen && context.brush == &stockBrush, "GDI selections not restored");
    require(liveObjects == 0, "GDI object leak");
    return context.calls;
}
Draft fixture() {
    Draft d;
    d.gumballVisible = true;
    d.gumballWorldSize = 82;
    d.gumballOrigin = {12, -8, 20};
    // Rotated orthonormal frame: detects a regression to world-axis glyphs.
    d.gumballAxisX = {0.6, 0.8, 0};
    d.gumballAxisY = {-0.8, 0.6, 0};
    d.gumballAxisZ = {0, 0, 1};
    return d;
}
std::vector<Call> select(const std::vector<Call>& calls, const std::string& kind) {
    std::vector<Call> result;
    for (const auto& c : calls) if (c.kind == kind) result.push_back(c);
    return result;
}
void strokeWidths() {
    auto d = fixture();
    for (bool threeD : {false, true}) {
        d.gumball3D = threeD;
        const auto calls = render(d);
        const auto rings = select(calls, "polyline"), axes = select(calls, "line"), planes = select(calls, "polygon");
        require(rings.size() == (threeD ? 3u : 2u), "rotation ring count");
        require(axes.size() == (threeD ? 9u : 6u), "axis/arrowhead count");
        require(planes.size() == (threeD ? 3u : 1u), "plane count");
        for (const auto& c : rings) require(c.width == 3, "rotation ring base width: expected 3px, got " + std::to_string(c.width));
        for (const auto& c : axes) require(c.width == 3, "axis base width must be 3px");
        for (const auto& c : planes) require(c.width == 2, "plane border base width must be 2px");
        require(select(calls, "ellipse").at(0).width == 3, "center ring base width must be 3px");
    }
    d.gumball3D = true;
    d.gumballHover = 6;
    require(select(render(d), "ellipse").at(0).width == 4, "center ring hover width must be 4px");
    for (int i = 0; i < 3; ++i) {
        d.gumballHover = i;
        auto axes = select(render(d), "line");
        for (int j = 0; j < 3; ++j) require(axes[j * 3].width == (i == j ? 5 : 3), "axis hover width must be 5px, others 3px");
        d.gumballHover = 7 + i;
        auto rings = select(render(d), "polyline");
        for (int j = 0; j < 3; ++j) require(rings[j].width == (i == j ? 4 : 3), "ring hover width must be 4px, others 3px");
        d.gumballHover = 3 + i;
        auto planes = select(render(d), "polygon");
        for (int j = 0; j < 3; ++j) require(planes[j].width == (i == j ? 3 : 2), "plane hover border must be 3px, others 2px");
    }
}
int channel(COLORREF color, int index) { return (color >> (8 * index)) & 255; }
bool hasHue(COLORREF color, int axis, int separation) {
    return channel(color, axis) >= channel(color, (axis + 1) % 3) + separation &&
           channel(color, axis) >= channel(color, (axis + 2) % 3) + separation;
}
int brightness(COLORREF c) { return channel(c, 0) + channel(c, 1) + channel(c, 2); }
double luminance(COLORREF color) {
    double values[3];
    for (int i = 0; i < 3; ++i) {
        double c = channel(color, i) / 255.0;
        values[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * values[0] + 0.7152 * values[1] + 0.0722 * values[2];
}
void colorsFollowAxesAndPlaneNormals() {
    const auto base = render(fixture());
    const auto baseAxes = select(base, "line");
    for (int i = 0; i < 3; ++i) {
        const auto color = baseAxes[i * 3].color;
        require(hasHue(color, i, 80), "axis must have a vivid RGB hue");
        const double contrast = (luminance(RGB(198, 224, 246)) + 0.05) / (luminance(color) + 0.05);
        require(contrast >= 3.0, "base axis contrast against pale canvas must be at least 3:1");
    }
    for (bool threeD : {false, true}) for (int hover = -1; hover <= 13; ++hover) {
        auto d = fixture();
        d.gumball3D = threeD;
        d.gumballHover = hover;
        const auto calls = render(d);
        const auto axes = select(calls, "line"), rings = select(calls, "polyline"),
                   planes = select(calls, "polygon"), boxes = select(calls, "rectangle");
        const int normal[3] = {2, 0, 1}; // XY -> Z, YZ -> X, ZX -> Y.
        for (int k = 0; k < (threeD ? 3 : 1); ++k) {
            require(hasHue(planes[k].fill, normal[k], 40), "plane fill must use its normal's RGB hue (XY blue, YZ red, ZX green)");
            require(hasHue(planes[k].color, normal[k], 80), "plane border must retain normal-axis hue on hover");
            require(brightness(planes[k].fill) > brightness(planes[k].color), "plane fill must be lighter than its border");
            if (hover != 3 + k) require(planes[k].color == baseAxes[normal[k] * 3].color, "plane border must match normal-axis color");
        }
        for (int i = 0; i < (threeD ? 3 : 2); ++i) {
            const auto baseColor = baseAxes[i * 3].color;
            const std::array<std::pair<COLORREF, int>, 3> handles = {{
                {axes[i * 3].color, i}, {rings[i].color, 7 + i}, {boxes[i].fill, 10 + i}
            }};
            for (auto [color, id] : handles) {
                require(hasHue(color, i, 80), "axis, ring and scale must retain RGB hue on hover");
                if (hover == id) require(brightness(color) > brightness(baseColor), "hover must lighten its axis color");
                else require(color == baseColor, "unhovered axis, ring and scale must share a color");
            }
        }
    }
}
void closedRotationRings() {
    for (bool threeD : {false, true}) for (int hover : {-1, 7, 8, 9}) {
        auto d = fixture();
        d.gumball3D = threeD;
        d.gumballHover = hover;
        for (const auto& c : select(render(d), "polyline")) {
            require(c.points.size() == 65, "ring needs 64 segments plus repeated first vertex; got " + std::to_string(c.points.size()) + " vertices");
            require(c.points.front() == c.points.back(), "rotation ring must close exactly");
        }
    }
}
void compactArrowheads() {
    for (bool threeD : {false, true}) for (int hover : {-1, 0, 1, 2}) {
        auto d = fixture();
        d.gumball3D = threeD;
        d.gumballHover = hover;
        const auto strokes = select(render(d), "line");
        for (int i = 0; i < (threeD ? 3 : 2); ++i) {
            const auto origin = strokes[i * 3].points[0], tip = strokes[i * 3].points[1];
            const double dx = tip.x - origin.x, dy = tip.y - origin.y;
            const double length = std::hypot(dx, dy);
            for (int side : {1, 2}) {
                const auto& head = strokes[i * 3 + side];
                require(head.points[0] == tip, "arrowhead must retain the existing tip");
                const double hx = tip.x - head.points[1].x, hy = tip.y - head.points[1].y;
                // Projection onto/perpendicular to shaft; tolerate integer truncation.
                const double depth = (hx * dx + hy * dy) / length;
                const double halfWidth = std::abs(hx * dy - hy * dx) / length;
                require(std::abs(depth - 12.0) <= 1.5, "arrowhead depth must be 12px, got " + std::to_string(depth));
                require(std::abs(halfWidth - 6.0) <= 1.5, "arrowhead half-width must be 6px");
            }
        }
    }
}
int main() {
    int passed = 0, failed = 0;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"base and hover stroke widths (2D/3D)", strokeWidths},
        {"RGB axes, normal-colored planes and hue-preserving hover", colorsFollowAxesAndPlaneNormals},
        {"closed rotation rings (2D/3D, base/hover)", closedRotationRings},
        {"compact 12x6px arrowheads", compactArrowheads},
    };
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << "Gumball renderer: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
