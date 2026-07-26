#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"

#include <optional>
#include <string_view>

namespace mm {

enum class DrawTool { Line, Polyline, Rectangle, Circle };
enum class SnapType { None, Grid, Endpoint, Midpoint };

struct SnapResult {
    Vec3 point{};
    SnapType type{SnapType::None};
    double distance{};
};

class SnapEngine {
public:
    static SnapResult snap(const Vec3& cursor, const Document& document,
                           double objectTolerance, double gridSpacing,
                           bool objectSnapEnabled = true, bool gridSnapEnabled = true);
    static SnapResult snap3D(const Vec2& screenCursor, const Document& document,
                             const Camera& camera, int viewportWidth, int viewportHeight,
                             double objectTolerancePixels, double gridSpacing, double workPlaneZ,
                             bool objectSnapEnabled = true, bool gridSnapEnabled = true);
};

std::optional<Vec3> parseDynamicPoint(std::wstring_view text,
                                      std::optional<Vec3> origin = std::nullopt) noexcept;
const wchar_t* snapTypeLabel(SnapType type) noexcept;
const wchar_t* toolLabel(DrawTool tool) noexcept;

} // namespace mm
