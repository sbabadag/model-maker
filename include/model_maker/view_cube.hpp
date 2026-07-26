#pragma once

#include "model_maker/camera.hpp"

#include <array>
#include <optional>

namespace mm {

struct CubePoint {
    int x{};
    int y{};
};

struct CubeRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

struct ViewCubeLayout {
    int centerX{};
    std::array<CubePoint, 4> topFace{};
    std::array<CubePoint, 4> frontFace{};
    std::array<CubePoint, 4> rightFace{};
    CubeRect leftControl{};
    CubeRect backControl{};
    CubeRect bottomControl{};
    CubeRect homeControl{};
};

class ViewCube {
public:
    static ViewCubeLayout layout(int viewportWidth) noexcept;
    static std::optional<StandardView> hitTest(int x, int y, int viewportWidth) noexcept;
};

} // namespace mm
