#pragma once

#include "model_maker/geometry.hpp"

#include <optional>

namespace mm {

enum class StandardView {
    Isometric,
    Top,
    Bottom,
    Front,
    Back,
    Left,
    Right
};

class Camera {
public:
    Vec2 project(Vec3 point, int viewportWidth, int viewportHeight) const noexcept;
    std::optional<Vec3> unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                         double planeZ) const noexcept;
    void rotate(double yawDelta, double pitchDelta) noexcept;
    void zoomBy(double factor) noexcept;
    void reset() noexcept;
    void setView(StandardView view) noexcept;

    double yaw() const noexcept;
    double pitch() const noexcept;
    double zoom() const noexcept;

private:
    double yaw_{-0.55};
    double pitch_{0.45};
    double zoom_{1.0};
    double pixelsPerUnit_{65.0};
};

} // namespace mm
