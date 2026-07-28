#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mm {
namespace {

bool contains(const CubeRect& rect, int x, int y) noexcept {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool contains(const std::array<CubePoint, 4>& polygon, int x, int y) noexcept {
    bool inside = false;
    std::size_t previous = polygon.size() - 1;
    for (std::size_t current = 0; current < polygon.size(); ++current) {
        const auto& a = polygon[current];
        const auto& b = polygon[previous];
        const bool crosses = (a.y > y) != (b.y > y);
        if (crosses) {
            const double crossingX = static_cast<double>(b.x - a.x) * (y - a.y) /
                                     static_cast<double>(b.y - a.y) + a.x;
            if (x < crossingX) inside = !inside;
        }
        previous = current;
    }
    return inside;
}

CubePoint projected(const Camera& camera, Vec3 point, int centerX, int centerY, double scale) noexcept {
    const Vec3 view = camera.viewTransform(point);
    return {static_cast<int>(std::lround(centerX + view.x * scale)),
            static_cast<int>(std::lround(centerY - view.y * scale))};
}

} // namespace

ViewCubeLayout ViewCube::layout(int viewportWidth, const Camera& camera) noexcept {
    ViewCubeLayout result;
    result.centerX = std::max(96, viewportWidth - 96);
    result.centerY = 119;
    result.bodyBounds = {result.centerX - 58, result.centerY - 58,
                         result.centerX + 58, result.centerY + 58};
    result.homeControl = {result.centerX + 19, 202, result.centerX + 65, 228};

    constexpr std::array<Vec3, 8> vertices{{
        {-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0}, {1.0, 1.0, -1.0}, {-1.0, 1.0, -1.0},
        {-1.0, -1.0, 1.0}, {1.0, -1.0, 1.0}, {1.0, 1.0, 1.0}, {-1.0, 1.0, 1.0}
    }};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        result.corners[i] = projected(camera, vertices[i], result.centerX, result.centerY, 31.0);

    struct FaceDefinition {
        StandardView view;
        std::array<std::size_t, 4> indices;
        Vec3 normal;
    };
    constexpr std::array<FaceDefinition, 6> definitions{{
        {StandardView::Left,   {0, 3, 7, 4}, {-1.0, 0.0, 0.0}},
        {StandardView::Right,  {1, 5, 6, 2}, { 1.0, 0.0, 0.0}},
        {StandardView::Bottom, {0, 4, 5, 1}, {0.0, -1.0, 0.0}},
        {StandardView::Top,    {3, 2, 6, 7}, {0.0,  1.0, 0.0}},
        {StandardView::Back,   {0, 1, 2, 3}, {0.0, 0.0, -1.0}},
        {StandardView::Front,  {4, 7, 6, 5}, {0.0, 0.0,  1.0}}
    }};
    for (std::size_t faceIndex = 0; faceIndex < definitions.size(); ++faceIndex) {
        const auto& definition = definitions[faceIndex];
        auto& face = result.faces[faceIndex];
        face.view = definition.view;
        for (std::size_t i = 0; i < definition.indices.size(); ++i)
            face.points[i] = result.corners[definition.indices[i]];
        const Vec3 normal = camera.viewTransform(definition.normal);
        face.visible = normal.z > 1e-6;
        face.depth = normal.z;
    }

    result.axisOrigin = {result.centerX - 47, 214};
    const auto axisPoint = [&](Vec3 axis) {
        const Vec3 view = camera.viewTransform(axis);
        return CubePoint{static_cast<int>(std::lround(result.axisOrigin.x + view.x * 19.0)),
                         static_cast<int>(std::lround(result.axisOrigin.y - view.y * 19.0))};
    };
    result.xAxis = axisPoint({1.0, 0.0, 0.0});
    result.yAxis = axisPoint({0.0, 1.0, 0.0});
    result.zAxis = axisPoint({0.0, 0.0, 1.0});
    return result;
}

std::optional<StandardView> ViewCube::hitTest(int x, int y, int viewportWidth,
                                              const Camera& camera) noexcept {
    const auto cube = layout(viewportWidth, camera);
    if (contains(cube.homeControl, x, y)) return StandardView::Isometric;

    const ViewCubeFace* closest = nullptr;
    for (const auto& face : cube.faces) {
        if (face.visible && contains(face.points, x, y) && (!closest || face.depth > closest->depth))
            closest = &face;
    }
    return closest ? std::optional<StandardView>{closest->view} : std::nullopt;
}

bool ViewCube::containsWidget(int x, int y, int viewportWidth) noexcept {
    const int center = std::max(96, viewportWidth - 96);
    const CubeRect bounds{center - 76, 48, center + 76, 238};
    return contains(bounds, x, y);
}

} // namespace mm
