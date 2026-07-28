#include "model_maker/camera.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

Vec2 Camera::project2D(Vec3 point, int viewportWidth, int viewportHeight) const noexcept {
    constexpr double pixelsPerUnit2D = 60.0;
    const double scale = pixelsPerUnit2D * zoom_;
    return {viewportWidth * 0.5 + (point.x - center2D_.x) * scale,
            viewportHeight * 0.5 - (point.y - center2D_.y) * scale};
}

Vec3 Camera::unproject2D(Vec2 screenPoint, int viewportWidth, int viewportHeight) const noexcept {
    constexpr double pixelsPerUnit2D = 60.0;
    const double scale = pixelsPerUnit2D * zoom_;
    return {center2D_.x + (screenPoint.x - viewportWidth * 0.5) / scale,
            center2D_.y + (viewportHeight * 0.5 - screenPoint.y) / scale,
            0.0};
}

Vec2 Camera::project(Vec3 point, int viewportWidth, int viewportHeight) const noexcept {
    const Vec3 rotated = viewTransform(point - center3D_);

    const double scale = pixelsPerUnit_ * zoom_;
    return {
        viewportWidth * 0.5 + rotated.x * scale,
        viewportHeight * 0.5 - rotated.y * scale
    };
}

Vec3 Camera::viewTransform(Vec3 point) const noexcept {
    const double cy = std::cos(yaw_);
    const double sy = std::sin(yaw_);
    const Vec3 yawed{point.x * cy + point.z * sy, point.y, -point.x * sy + point.z * cy};

    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);
    return {yawed.x, yawed.y * cp - yawed.z * sp, yawed.y * sp + yawed.z * cp};
}

std::optional<Vec3> Camera::unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                              double planeZ) const noexcept {
    return unprojectToPlane(screenPoint, viewportWidth, viewportHeight,
                            WorkPlane{{0.0, 0.0, planeZ}, {1.0, 0.0, 0.0},
                                      {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}});
}

std::optional<Vec3> Camera::unprojectToPlane(Vec2 screenPoint, int viewportWidth, int viewportHeight,
                                              const WorkPlane& plane) const noexcept {
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

    const Vec3 origin = center3D_ + inverseRotate({cameraX, cameraY, -1.0});
    const Vec3 direction = inverseRotate({0.0, 0.0, 1.0});
    const auto dot = [](const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const double denominator = dot(direction, plane.normal);
    if (std::abs(denominator) < 1e-9) return std::nullopt;
    const double rayDistance = dot(plane.origin - origin, plane.normal) / denominator;
    return origin + direction * rayDistance;
}

void Camera::rotate(double yawDelta, double pitchDelta) noexcept {
    yaw_ += yawDelta;
    pitch_ = std::clamp(pitch_ + pitchDelta, -1.5, 1.5);
}

void Camera::zoomBy(double factor) noexcept {
    zoom_ = std::clamp(zoom_ * factor, 1e-9, 8.0);
}

void Camera::zoom2DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept {
    if (viewportWidth <= 0 || viewportHeight <= 0) { zoomBy(factor); return; }
    const Vec3 before = unproject2D(screenPoint, viewportWidth, viewportHeight);
    zoomBy(factor);
    const Vec3 after = unproject2D(screenPoint, viewportWidth, viewportHeight);
    center2D_.x += before.x - after.x;
    center2D_.y += before.y - after.y;
}

void Camera::zoom3DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept {
    if (viewportWidth <= 0 || viewportHeight <= 0) { zoomBy(factor); return; }
    const double oldScale = pixelsPerUnit_ * zoom_;
    const double oldX = (screenPoint.x - viewportWidth * 0.5) / oldScale;
    const double oldY = (viewportHeight * 0.5 - screenPoint.y) / oldScale;
    zoomBy(factor);
    const double newScale = pixelsPerUnit_ * zoom_;
    const double newX = (screenPoint.x - viewportWidth * 0.5) / newScale;
    const double newY = (viewportHeight * 0.5 - screenPoint.y) / newScale;
    const double cy = std::cos(yaw_), sy = std::sin(yaw_);
    const double cp = std::cos(pitch_), sp = std::sin(pitch_);
    const Vec3 cameraDelta{oldX - newX, oldY - newY, 0.0};
    const Vec3 yawed{cameraDelta.x, cameraDelta.y * cp + cameraDelta.z * sp,
                     -cameraDelta.y * sp + cameraDelta.z * cp};
    center3D_ = center3D_ + Vec3{yawed.x * cy - yawed.z * sy, yawed.y,
                                 yawed.x * sy + yawed.z * cy};
}

void Camera::pan2DByPixels(double deltaX, double deltaY) noexcept {
    constexpr double pixelsPerUnit2D = 60.0;
    const double scale = pixelsPerUnit2D * zoom_;
    if (scale <= 0.0) return;
    center2D_.x -= deltaX / scale;
    center2D_.y += deltaY / scale;
}

bool Camera::fit2D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
                   double marginPixels) noexcept {
    constexpr double pixelsPerUnit2D = 60.0;
    if (viewportWidth <= 0 || viewportHeight <= 0 || maximum.x < minimum.x || maximum.y < minimum.y)
        return false;
    const double usableWidth = viewportWidth - 2.0 * std::max(0.0, marginPixels);
    const double usableHeight = viewportHeight - 2.0 * std::max(0.0, marginPixels);
    if (usableWidth <= 0.0 || usableHeight <= 0.0) return false;
    const double worldWidth = std::max(maximum.x - minimum.x, 1e-6);
    const double worldHeight = std::max(maximum.y - minimum.y, 1e-6);
    zoom_ = std::clamp(std::min(usableWidth / (worldWidth * pixelsPerUnit2D),
                               usableHeight / (worldHeight * pixelsPerUnit2D)), 1e-9, 8.0);
    center2D_ = {(minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5};
    return true;
}

bool Camera::fit3D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
                   double marginPixels) noexcept {
    if (viewportWidth <= 0 || viewportHeight <= 0 || maximum.x < minimum.x ||
        maximum.y < minimum.y || maximum.z < minimum.z) return false;
    const double usableWidth = viewportWidth - 2.0 * std::max(0.0, marginPixels);
    const double usableHeight = viewportHeight - 2.0 * std::max(0.0, marginPixels);
    if (usableWidth <= 0.0 || usableHeight <= 0.0) return false;
    center3D_ = {(minimum.x + maximum.x) * 0.5,
                 (minimum.y + maximum.y) * 0.5,
                 (minimum.z + maximum.z) * 0.5};
    double minimumX{}, maximumX{}, minimumY{}, maximumY{};
    bool first = true;
    for (double x : {minimum.x, maximum.x}) {
        for (double y : {minimum.y, maximum.y}) {
            for (double z : {minimum.z, maximum.z}) {
                const Vec3 rotated = viewTransform(Vec3{x, y, z} - center3D_);
                if (first) {
                    minimumX = maximumX = rotated.x;
                    minimumY = maximumY = rotated.y;
                    first = false;
                } else {
                    minimumX = std::min(minimumX, rotated.x);
                    maximumX = std::max(maximumX, rotated.x);
                    minimumY = std::min(minimumY, rotated.y);
                    maximumY = std::max(maximumY, rotated.y);
                }
            }
        }
    }
    const double projectedWidth = std::max(maximumX - minimumX, 1e-9);
    const double projectedHeight = std::max(maximumY - minimumY, 1e-9);
    zoom_ = std::clamp(std::min(usableWidth / (projectedWidth * pixelsPerUnit_),
                               usableHeight / (projectedHeight * pixelsPerUnit_)), 1e-9, 8.0);
    return true;
}

void Camera::reset() noexcept {
    yaw_ = -0.55;
    pitch_ = 0.45;
    zoom_ = 1.0;
    center2D_ = {};
    center3D_ = {};
}

void Camera::setView(StandardView view) noexcept {
    constexpr double halfPi = 1.57079632679489661923;
    constexpr double pi = 3.14159265358979323846;
    switch (view) {
    case StandardView::Isometric:
        yaw_ = -0.78539816339744830962;
        pitch_ = 0.61547970867038734107;
        break;
    case StandardView::Top: yaw_ = 0.0; pitch_ = halfPi; break;
    case StandardView::Bottom: yaw_ = 0.0; pitch_ = -halfPi; break;
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
