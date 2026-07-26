#include "model_maker/drafting.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace mm {
namespace {
double distance2D(const Vec3& a, const Vec3& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

std::optional<double> parseNumber(std::wstring_view text) noexcept {
    try {
        std::wstring value(text);
        std::size_t used{};
        const double result = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(result)) return std::nullopt;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}
}

SnapResult SnapEngine::snap(const Vec3& cursor, const Document& document,
                            double objectTolerance, double gridSpacing,
                            bool objectSnapEnabled, bool gridSnapEnabled) {
    SnapResult best{cursor, SnapType::None, objectTolerance};
    bool found = false;
    auto consider = [&](const Vec3& point, SnapType type) {
        const double distance = distance2D(cursor, point);
        if (distance <= objectTolerance && (!found || distance < best.distance ||
            (distance == best.distance && type == SnapType::Endpoint))) {
            best = {point, type, distance};
            found = true;
        }
    };

    if (objectSnapEnabled) {
        for (const auto& model : document.models()) {
            for (const auto& vertex : model.vertices()) consider(vertex, SnapType::Endpoint);
            for (const auto& edge : model.edges()) {
                const Vec3& a = model.vertices()[edge.from];
                const Vec3& b = model.vertices()[edge.to];
                consider({(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5}, SnapType::Midpoint);
            }
        }
    }
    if (found) return best;
    if (gridSnapEnabled && gridSpacing > 0.0) {
        Vec3 grid{std::round(cursor.x / gridSpacing) * gridSpacing,
                  std::round(cursor.y / gridSpacing) * gridSpacing, 0.0};
        return {grid, SnapType::Grid, distance2D(cursor, grid)};
    }
    return {cursor, SnapType::None, 0.0};
}

SnapResult SnapEngine::snap3D(const Vec2& screenCursor, const Document& document,
                              const Camera& camera, int viewportWidth, int viewportHeight,
                              double objectTolerancePixels, double gridSpacing, double workPlaneZ,
                              bool objectSnapEnabled, bool gridSnapEnabled) {
    SnapResult best{};
    best.distance = objectTolerancePixels;
    bool found = false;
    auto consider = [&](const Vec3& point, SnapType type) {
        const Vec2 projected = camera.project(point, viewportWidth, viewportHeight);
        const double distance = std::hypot(screenCursor.x - projected.x, screenCursor.y - projected.y);
        if (distance <= objectTolerancePixels && (!found || distance < best.distance ||
            (distance == best.distance && type == SnapType::Endpoint))) {
            best = {point, type, distance};
            found = true;
        }
    };

    if (objectSnapEnabled) {
        for (const auto& model : document.models()) {
            for (const auto& vertex : model.vertices()) consider(vertex, SnapType::Endpoint);
            for (const auto& edge : model.edges()) {
                const Vec3& a = model.vertices()[edge.from];
                const Vec3& b = model.vertices()[edge.to];
                consider({(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5},
                         SnapType::Midpoint);
            }
        }
    }
    if (found) return best;

    const auto raw = camera.unprojectToPlane(screenCursor, viewportWidth, viewportHeight, workPlaneZ);
    if (!raw) return {{0.0, 0.0, workPlaneZ}, SnapType::None, 0.0};
    if (gridSnapEnabled && gridSpacing > 0.0) {
        const Vec3 grid{std::round(raw->x / gridSpacing) * gridSpacing,
                        std::round(raw->y / gridSpacing) * gridSpacing, workPlaneZ};
        const Vec2 projected = camera.project(grid, viewportWidth, viewportHeight);
        return {grid, SnapType::Grid,
                std::hypot(screenCursor.x - projected.x, screenCursor.y - projected.y)};
    }
    return {*raw, SnapType::None, 0.0};
}

std::optional<Vec3> parseDynamicPoint(std::wstring_view text, std::optional<Vec3> origin) noexcept {
    const auto comma = text.find(L',');
    if (comma != std::wstring_view::npos) {
        const auto secondComma = text.find(L',', comma + 1);
        const auto x = parseNumber(text.substr(0, comma));
        const auto y = parseNumber(text.substr(comma + 1,
                                  secondComma == std::wstring_view::npos
                                      ? std::wstring_view::npos
                                      : secondComma - comma - 1));
        if (!x || !y) return std::nullopt;
        if (secondComma != std::wstring_view::npos) {
            const auto z = parseNumber(text.substr(secondComma + 1));
            if (!z) return std::nullopt;
            return Vec3{*x, *y, *z};
        }
        return Vec3{*x, *y, 0.0};
    }
    const auto angleMark = text.find(L'<');
    if (angleMark != std::wstring_view::npos && origin) {
        const auto distance = parseNumber(text.substr(0, angleMark));
        const auto angle = parseNumber(text.substr(angleMark + 1));
        if (!distance || !angle || *distance < 0.0) return std::nullopt;
        const double radians = *angle * std::numbers::pi / 180.0;
        return Vec3{origin->x + *distance * std::cos(radians),
                    origin->y + *distance * std::sin(radians), origin->z};
    }
    return std::nullopt;
}

const wchar_t* snapTypeLabel(SnapType type) noexcept {
    switch (type) {
    case SnapType::Grid: return L"GRID";
    case SnapType::Endpoint: return L"ENDPOINT";
    case SnapType::Midpoint: return L"MIDPOINT";
    default: return L"";
    }
}

const wchar_t* toolLabel(DrawTool tool) noexcept {
    switch (tool) {
    case DrawTool::Line: return L"LINE";
    case DrawTool::Polyline: return L"POLYLINE";
    case DrawTool::Rectangle: return L"RECTANGLE";
    case DrawTool::Circle: return L"CIRCLE";
    }
    return L"";
}

} // namespace mm
