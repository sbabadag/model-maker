#pragma once

#include "model_maker/geometry.hpp"

#include <array>
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
    void setOrbitCenter(const Vec3& worldPoint) noexcept;
    void zoomBy(double factor) noexcept;
    void zoom2DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept;
    void zoom3DAt(Vec2 screenPoint, double factor, int viewportWidth, int viewportHeight) noexcept;
    void pan2DByPixels(double deltaX, double deltaY) noexcept;
    void pan3DByPixels(double deltaX, double deltaY) noexcept;
    bool fit2D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
               double marginPixels = 40.0) noexcept;
    bool fit3D(Vec3 minimum, Vec3 maximum, int viewportWidth, int viewportHeight,
               double marginPixels = 40.0) noexcept;
    void reset() noexcept;
    void setView(StandardView view) noexcept;

    // Euler acilari artik TURETILMIS degerlerdir (her cagride matristen
    // ayristirilir) — yalnizca test/durum gosterimi icin.
    double yaw() const noexcept;
    double pitch() const noexcept;
    double roll() const noexcept;
    double zoom() const noexcept;
    double pixelsPerUnit() const noexcept;

    // 3Dconnexion Navlib koprusu: navlib camera-to-world 4x4 matris ister
    // (m[row*4+col], rotasyon = R^T, translasyon = pozisyon). Kamera yonu
    // artik SUREKLI ROTASYON MATRISI R_ ile tutulur (row-major R_[3*i+j],
    // world->camera): euler ayrismasinin kutup devrilmesi (gimbal) ve isaret
    // dal degistirme ziplamalari kokten biter. apply her matrisi
    // orthonormalize eder (drift/shear birikmez, det her zaman +1).
    std::array<double, 16> cameraToWorldMatrix4() const noexcept;
    void applyCameraToWorldMatrix4(const std::array<double, 16>& matrix) noexcept;

    const Vec3& center3D() const noexcept { return center3D_; }
    void setCenter3D(const Vec3& center) noexcept { center3D_ = center; }

private:
    // world->camera rotasyonu, row-major: v_cam = R_ * v_world.
    std::array<double, 9> R_{1.0, 0.0, 0.0,
                            0.0, 1.0, 0.0,
                            0.0, 0.0, 1.0};
    double zoom_{1.0};
    double pixelsPerUnit_{65.0};
    Vec2 center2D_{};
    Vec3 center3D_{};

    static std::array<double, 9> buildRotation(double yaw, double pitch, double roll) noexcept;
    void decompose(double& yaw, double& pitch, double& roll) const noexcept;
};

} // namespace mm
