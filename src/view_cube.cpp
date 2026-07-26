#include "model_maker/view_cube.hpp"

#include <algorithm>

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

} // namespace

ViewCubeLayout ViewCube::layout(int viewportWidth) noexcept {
    const int center = std::max(84, viewportWidth - 84);
    return {
        center,
        {{{center, 82}, {center + 38, 104}, {center, 126}, {center - 38, 104}}},
        {{{center - 38, 104}, {center, 126}, {center, 174}, {center - 38, 152}}},
        {{{center, 126}, {center + 38, 104}, {center + 38, 152}, {center, 174}}},
        {center - 62, 134, center - 46, 150},
        {center - 8, 60, center + 8, 76},
        {center - 8, 180, center + 8, 196},
        {center - 25, 205, center + 25, 231}
    };
}

std::optional<StandardView> ViewCube::hitTest(int x, int y, int viewportWidth) noexcept {
    const auto cube = layout(viewportWidth);
    if (contains(cube.topFace, x, y)) return StandardView::Top;
    if (contains(cube.frontFace, x, y)) return StandardView::Front;
    if (contains(cube.rightFace, x, y)) return StandardView::Right;
    if (contains(cube.leftControl, x, y)) return StandardView::Left;
    if (contains(cube.backControl, x, y)) return StandardView::Back;
    if (contains(cube.bottomControl, x, y)) return StandardView::Bottom;
    if (contains(cube.homeControl, x, y)) return StandardView::Isometric;
    return std::nullopt;
}

} // namespace mm
