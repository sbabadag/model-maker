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
    Vec3 viewTransform(Vec3 vector) const noexcept;
    Vec2 project2D(Vec3 point, int viewportWidth, int viewportHeight) const noexcept;
    Vec3 unproject2D(Vec2 screenPoint, int viewportWidth, int viewportHeight) const noexcept;
    std::optional<Vec3> unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                         double planeZ) const noexcept;
    std::optional<Vec3> unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                         const WorkPlane& plane) const noexcept;
    void rotate(double yawDelta, double pitchDelta) noexcept;
    void zoomBy(double factor) noexcept;
    void zoom2DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept;
    void zoom3DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept;
    void pan2DByPixels(double deltaX, double deltaY) noexcept;
    bool fit2D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
               double marginPixels = 40.0) noexcept;
    bool fit3D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
               double marginPixels = 40.0) noexcept;
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
    Vec2 center2D_{};
    Vec3 center3D_{};
};

} // namespace mm
