#pragma once

#include "model_maker/camera.hpp"

#include <array>
#include <optional>

namespace mm {

struct CubePoint {
    int x{};
    int y{};
    bool operator==(const CubePoint&) const = default;
};

struct CubeRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

struct ViewCubeFace {
    StandardView view{StandardView::Front};
    std::array<CubePoint, 4> points{};
    bool visible{};
    double depth{};
};

struct ViewCubeLayout {
    int centerX{};
    int centerY{};
    std::array<CubePoint, 8> corners{};
    std::array<ViewCubeFace, 6> faces{};
    CubeRect bodyBounds{};
    CubeRect homeControl{};
    CubePoint axisOrigin{};
    CubePoint xAxis{};
    CubePoint yAxis{};
    CubePoint zAxis{};
};

class ViewCube {
public:
    static ViewCubeLayout layout(int viewportWidth, const Camera& camera) noexcept;
    static std::optional<StandardView> hitTest(int x, int y, int viewportWidth,
                                               const Camera& camera) noexcept;
    static bool containsWidget(int x, int y, int viewportWidth) noexcept;
};

} // namespace mm
