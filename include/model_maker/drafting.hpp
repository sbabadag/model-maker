#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mm {

enum class DrawTool { Line, Polyline, Rectangle, Circle };
enum class SnapType {
    None, Grid, Endpoint, Midpoint, Center, GeometricCenter, Node, Quadrant,
    Intersection, ApparentIntersection, Extension, Insertion, Perpendicular,
    Tangent, Nearest, Parallel
};
enum class SnapMarkerSymbol {
    None, GridCross, Square, Triangle, Circle, CircleCross, CrossedCircle, Diamond, Cross,
    ExtensionLine, LinkedSquares, RightAngle, TangentCircle, Hourglass, BoxedCross, ParallelLines
};
using SnapTypeMask = std::array<bool, static_cast<std::size_t>(SnapType::Parallel) + 1>;

struct SnapResult {
    Vec3 point{};
    SnapType type{SnapType::None};
    double distance{};
};

struct EntityStyleSelection {
    std::string layer{"0"};
    std::optional<std::uint32_t> trueColor;
    std::string lineType{"BYLAYER"};
};

EntityProperties resolveEntityStyle(
    const EntityStyleSelection& selection,
    const std::unordered_map<std::string, EntityProperties>& layers);

class SnapEngine {
public:
    static SnapResult snap(const Vec3& cursor, const Document& document,
                           double objectTolerance, double gridSpacing,
                           bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                           std::optional<Vec3> referencePoint = std::nullopt,
                           const SnapTypeMask* enabledTypes = nullptr);
    static SnapResult snap3D(const Vec2& screenCursor, const Document& document,
                             const Camera& camera, int viewportWidth, int viewportHeight,
                             double objectTolerancePixels, double gridSpacing, double workPlaneZ,
                             bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                             std::optional<Vec3> referencePoint = std::nullopt,
                             const SnapTypeMask* enabledTypes = nullptr);
    static SnapResult snap3D(const Vec2& screenCursor, const Document& document,
                             const Camera& camera, int viewportWidth, int viewportHeight,
                             double objectTolerancePixels, double gridSpacing, const WorkPlane& workPlane,
                             bool objectSnapEnabled = true, bool gridSnapEnabled = true,
                             std::optional<Vec3> referencePoint = std::nullopt,
                             const SnapTypeMask* enabledTypes = nullptr);
};

std::optional<Vec3> parseDynamicPoint(std::wstring_view text,
                                      std::optional<Vec3> origin = std::nullopt,
                                      std::optional<Vec3> directionPoint = std::nullopt) noexcept;
const wchar_t* snapTypeLabel(SnapType type) noexcept;
SnapMarkerSymbol snapMarkerSymbol(SnapType type) noexcept;
const wchar_t* toolLabel(DrawTool tool) noexcept;
bool shouldEvaluateSnapping(bool selectingEntities, bool zoomPhase, bool cameraNavigating) noexcept;
Vec3 constrainOrtho(const Vec3& anchor, const Vec3& cursor) noexcept;
SnapResult applyOrtho(const Vec3& anchor, SnapResult candidate,
                      bool preserveObjectSnaps = true) noexcept;
Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight) noexcept;
Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight, const WorkPlane& workPlane,
                      bool includePlaneNormal = false) noexcept;
SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight,
                        bool preserveObjectSnaps = true) noexcept;
SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight,
                        const WorkPlane& workPlane, bool includePlaneNormal = false,
                        bool preserveObjectSnaps = true) noexcept;
SnapResult applyPolarTracking(const Vec3& anchor, SnapResult candidate,
                              double incrementDegrees = 90.0, double apertureDegrees = 12.0,
                              bool preserveObjectSnaps = true) noexcept;
SnapResult applyPolarTracking(const Vec3& anchor, SnapResult candidate, const WorkPlane& plane,
                              double incrementDegrees = 90.0, double apertureDegrees = 12.0,
                              bool preserveObjectSnaps = true) noexcept;
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
std::optional<WireframeModel> offsetModel2D(const WireframeModel& source, double distance,
                                            const Vec3& sidePoint);
std::optional<WireframeModel> mirrorModel2D(const WireframeModel& source, const Vec3& axisStart,
                                            const Vec3& axisEnd);
std::vector<WireframeModel> linearArray2D(const WireframeModel& source, std::size_t itemCount,
                                          const Vec3& spacing);
std::vector<WireframeModel> polarArray2D(const WireframeModel& source, std::size_t itemCount,
                                         const Vec3& center);
std::optional<std::vector<WireframeModel>> trimLine2D(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint);
std::optional<WireframeModel> extendLine2D(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint);
std::optional<WireframeModel> offsetModelOnPlane(const WireframeModel& source, double distance,
                                                 const Vec3& sidePoint, const WorkPlane& plane);
std::optional<WireframeModel> mirrorModelOnPlane(const WireframeModel& source,
                                                 const Vec3& axisStart, const Vec3& axisEnd,
                                                 const WorkPlane& plane);
std::vector<WireframeModel> polarArrayOnPlane(const WireframeModel& source, std::size_t itemCount,
                                              const Vec3& center, const WorkPlane& plane);
std::optional<std::vector<WireframeModel>> trimLineOnPlane(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint, const WorkPlane& plane);
std::optional<WireframeModel> extendLineOnPlane(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint, const WorkPlane& plane);

} // namespace mm
