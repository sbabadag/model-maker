#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace mm {

enum class DrawTool { Line, Polyline, Rectangle, Circle };
enum class SnapType {
    None, Grid, Endpoint, Midpoint, Center, GeometricCenter, Node, Quadrant,
    Intersection, ApparentIntersection, Extension, Insertion, Perpendicular,
    Tangent, Nearest, Parallel
};

struct SnapResult {
    Vec3 point{};
    SnapType type{SnapType::None};
    double distance{};
};

class SnapEngine {
public:
    static SnapResult snap(const Vec3& cursor, const Document& document,
                           double objectTolerance, double gridSpacing,
                           bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                           std::optional<Vec3> referencePoint = std::nullopt);
    static SnapResult snap3D(const Vec2& screenCursor, const Document& document,
                             const Camera& camera, int viewportWidth, int viewportHeight,
                             double objectTolerancePixels, double gridSpacing, double workPlaneZ,
                             bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                             std::optional<Vec3> referencePoint = std::nullopt);
    static SnapResult snap3D(const Vec2& screenCursor, const Document& document,
                             const Camera& camera, int viewportWidth, int viewportHeight,
                             double objectTolerancePixels, double gridSpacing, const WorkPlane& workPlane,
                             bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                             std::optional<Vec3> referencePoint = std::nullopt);
};

std::optional<Vec3> parseDynamicPoint(std::wstring_view text,
                                      std::optional<Vec3> origin = std::nullopt,
                                      std::optional<Vec3> directionPoint = std::nullopt) noexcept;
const wchar_t* snapTypeLabel(SnapType type) noexcept;
const wchar_t* toolLabel(DrawTool tool) noexcept;
bool shouldEvaluateSnapping(bool selectingEntities, bool zoomPhase, bool cameraNavigating) noexcept;
Vec3 constrainOrtho(const Vec3& anchor, const Vec3& cursor) noexcept;
SnapResult applyOrtho(const Vec3& anchor, SnapResult candidate) noexcept;
Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight) noexcept;
SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight) noexcept;
std::optional<std::size_t> hitTestModel2D(const Vec3& cursor, const Document& document,
                                         double tolerance);
std::optional<std::size_t> hitTestModel3D(const Vec2& cursor, const Document& document,
                                         const Camera& camera, int viewportWidth, int viewportHeight,
                                         double tolerancePixels);
std::vector<std::size_t> selectModelsInRect2D(const Vec3& firstCorner, const Vec3& secondCorner,
                                              const Document& document, bool crossing);
std::vector<std::size_t> selectModelsInRect3D(const Vec2& firstCorner, const Vec2& secondCorner,
                                              const Document& document, const Camera& camera,
                                              int viewportWidth, int viewportHeight, bool crossing);

} // namespace mm
