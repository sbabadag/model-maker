#include "model_maker/camera.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

Vec2 Camera::project(Vec3 point, int viewportWidth, int viewportHeight) const noexcept {
    const double cy = std::cos(yaw_);
    const double sy = std::sin(yaw_);
    const Vec3 yawed{point.x * cy + point.z * sy, point.y, -point.x * sy + point.z * cy};

    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);
    const Vec3 rotated{yawed.x, yawed.y * cp - yawed.z * sp, yawed.y * sp + yawed.z * cp};

    const double scale = pixelsPerUnit_ * zoom_;
    return {
        viewportWidth * 0.5 + rotated.x * scale,
        viewportHeight * 0.5 - rotated.y * scale
    };
}

std::optional<Vec3> Camera::unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                              double planeZ) const noexcept {
    if (viewportWidth <= 0 || viewportHeight <= 0 || zoom_ <= 0.0) return std::nullopt;

    const double scale = pixelsPerUnit_ * zoom_;
    const double cameraX = (screenPoint.x - viewportWidth * 0.5) / scale;
    const double cameraY = (viewportHeight * 0.5 - screenPoint.y) / scale;
    const double cy = std::cos(yaw_);
    const double sy = std::sin(yaw_);
    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);

    const auto inverseRotate = [&](const Vec3& point) {
        const Vec3 yawed{point.x, point.y * cp + point.z * sp,
                         -point.y * sp + point.z * cp};
        return Vec3{yawed.x * cy - yawed.z * sy, yawed.y,
                    yawed.x * sy + yawed.z * cy};
    };

    const Vec3 origin = inverseRotate({cameraX, cameraY, -1.0});
    const Vec3 direction = inverseRotate({0.0, 0.0, 1.0});
    if (std::abs(direction.z) < 1e-9) return std::nullopt;
    const double rayDistance = (planeZ - origin.z) / direction.z;
    return origin + direction * rayDistance;
}

void Camera::rotate(double yawDelta, double pitchDelta) noexcept {
    yaw_ += yawDelta;
    pitch_ = std::clamp(pitch_ + pitchDelta, -1.5, 1.5);
}

void Camera::zoomBy(double factor) noexcept {
    zoom_ = std::clamp(zoom_ * factor, 0.15, 8.0);
}

void Camera::reset() noexcept {
    yaw_ = -0.55;
    pitch_ = 0.45;
    zoom_ = 1.0;
}

void Camera::setView(StandardView view) noexcept {
    constexpr double halfPi = 1.57079632679489661923;
    constexpr double pi = 3.14159265358979323846;
    switch (view) {
    case StandardView::Isometric:
        yaw_ = -0.78539816339744830962;
        pitch_ = 0.61547970867038734107;
        break;
    case StandardView::Top: yaw_ = 0.0; pitch_ = 1.5; break;
    case StandardView::Bottom: yaw_ = 0.0; pitch_ = -1.5; break;
    case StandardView::Front: yaw_ = 0.0; pitch_ = 0.0; break;
    case StandardView::Back: yaw_ = pi; pitch_ = 0.0; break;
    case StandardView::Left: yaw_ = halfPi; pitch_ = 0.0; break;
    case StandardView::Right: yaw_ = -halfPi; pitch_ = 0.0; break;
    }
}

double Camera::yaw() const noexcept { return yaw_; }
double Camera::pitch() const noexcept { return pitch_; }
double Camera::zoom() const noexcept { return zoom_; }

} // namespace mm
