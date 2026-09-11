#include "model_maker/camera.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

namespace {

inline double dot3row(const double* row, const Vec3& v) {
    return row[0] * v.x + row[1] * v.y + row[2] * v.z;
}

std::array<double, 9> mat3mul(const std::array<double, 9>& a, const std::array<double, 9>& b) noexcept {
    std::array<double, 9> c{};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            c[static_cast<std::size_t>(r * 3 + col)] =
                a[static_cast<std::size_t>(r * 3)] * b[static_cast<std::size_t>(col)] +
                a[static_cast<std::size_t>(r * 3 + 1)] * b[static_cast<std::size_t>(3 + col)] +
                a[static_cast<std::size_t>(r * 3 + 2)] * b[static_cast<std::size_t>(6 + col)];
    return c;
}

double rowLen(const std::array<double, 9>& m, int row) noexcept {
    return std::sqrt(m[static_cast<std::size_t>(row * 3)] * m[static_cast<std::size_t>(row * 3)] +
                     m[static_cast<std::size_t>(row * 3 + 1)] * m[static_cast<std::size_t>(row * 3 + 1)] +
                     m[static_cast<std::size_t>(row * 3 + 2)] * m[static_cast<std::size_t>(row * 3 + 2)]);
}

// Gram-Schmidt: satir0 normalize, satir1 ortogonalize+normalize, satir2 =
// satir0 x satir1 (her zaman proper, det=+1; olcek/shear/mirror birikmez).
void orthonormalize(std::array<double, 9>& m) noexcept {
    const double n0 = rowLen(m, 0);
    if (n0 < 1e-12) return;
    for (int i = 0; i < 3; ++i) m[static_cast<std::size_t>(i)] /= n0;
    const double d01 = m[0] * m[3] + m[1] * m[4] + m[2] * m[5];
    for (int i = 0; i < 3; ++i) m[static_cast<std::size_t>(3 + i)] -= d01 * m[static_cast<std::size_t>(i)];
    const double n1 = rowLen(m, 1);
    if (n1 < 1e-12) return;
    for (int i = 0; i < 3; ++i) m[static_cast<std::size_t>(3 + i)] /= n1;
    m[6] = m[1] * m[5] - m[2] * m[4];
    m[7] = m[2] * m[3] - m[0] * m[5];
    m[8] = m[0] * m[4] - m[1] * m[3];
}

} // namespace

// world->camera rotasyonu: R = R_roll * R_pitch * R_yaw (eski euler kamerasi
// ile ayni carpim sirasi — piksel birebir uyum).
std::array<double, 9> Camera::buildRotation(double yaw, double pitch, double roll) noexcept {
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cr = std::cos(roll), sr = std::sin(roll);
    return {
        cr * cy - sr * sp * sy, -sr * cp, cr * sy + sr * sp * cy,
        sr * cy + cr * sp * sy,  cr * cp, sr * sy - cr * sp * cy,
        -cp * sy,                sp,      cp * cy
    };
}

// R_ -> euler (yalnizca durum gosterimi/test; hareket yolunda KULLANILMAZ).
// R_[7] = R[2][1] = sp; R_[6] = -cp*sy; R_[8] = cp*cy; R_[1] = -sr*cp; R_[4] = cr*cp.
void Camera::decompose(double& yaw, double& pitch, double& roll) const noexcept {
    pitch = std::asin(std::clamp(R_[7], -1.0, 1.0));
    const double cp = std::cos(pitch);
    if (cp > 1e-8) {
        yaw = std::atan2(-R_[6], R_[8]);
        roll = std::atan2(-R_[1], R_[4]);
    } else {
        yaw = std::atan2(R_[3], R_[0]);
        roll = 0.0;
    }
}

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

Vec3 Camera::viewTransform(Vec3 point) const noexcept {
    return {dot3row(&R_[0], point),
            dot3row(&R_[3], point),
            dot3row(&R_[6], point)};
}

Vec2 Camera::project(Vec3 point, int viewportWidth, int viewportHeight) const noexcept {
    const Vec3 rotated = viewTransform(point - center3D_);
    const double scale = pixelsPerUnit_ * zoom_;
    return {
        viewportWidth * 0.5 + rotated.x * scale,
        viewportHeight * 0.5 - rotated.y * scale
    };
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

    // Kamera->dunya: R^T (R'nin satirlari = dunyada kamera eksenleri).
    // Ileri bakis yonu = -satir2 (kamera -Z ileri); isin: origin + t*dir.
    const Vec3 origin = center3D_ + Vec3{
        R_[0] * cameraX + R_[3] * cameraY - R_[6],
        R_[1] * cameraX + R_[4] * cameraY - R_[7],
        R_[2] * cameraX + R_[5] * cameraY - R_[8]};
    const Vec3 direction{-R_[6], -R_[7], -R_[8]};
    const auto dot = [](const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const double denominator = dot(direction, plane.normal);
    if (std::abs(denominator) < 1e-9) return std::nullopt;
    const double rayDistance = dot(plane.origin - origin, plane.normal) / denominator;
    return origin + direction * rayDistance;
}

void Camera::rotate(double yawDelta, double pitchDelta) noexcept {
    // Surekli artis: pitch kamera X ekseni etrafinda (soldan carpma), yaw
    // dunya Y ekseni etrafinda (sagdan carpma). Roll=0 iken eski euler
    // toplama davranisiyla birebir esdeger; kutup/devrilme yok.
    const double cp = std::cos(pitchDelta), sp = std::sin(pitchDelta);
    const double cy = std::cos(yawDelta), sy = std::sin(yawDelta);
    const std::array<double, 9> rx{1.0, 0.0, 0.0,
                                   0.0, cp, -sp,
                                   0.0, sp, cp};
    const std::array<double, 9> ry{cy, 0.0, sy,
                                   0.0, 1.0, 0.0,
                                   -sy, 0.0, cy};
    R_ = mat3mul(rx, mat3mul(R_, ry));
}

void Camera::setOrbitCenter(const Vec3& worldPoint) noexcept {
    center3D_ = worldPoint;
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
    // Gorunum-uzayi delta -> dunya: R^T * delta (dz = 0).
    const double dx = oldX - newX, dy = oldY - newY;
    center3D_ = center3D_ + Vec3{
        R_[0] * dx + R_[3] * dy,
        R_[1] * dx + R_[4] * dy,
        R_[2] * dx + R_[5] * dy};
}

void Camera::pan2DByPixels(double deltaX, double deltaY) noexcept {
    constexpr double pixelsPerUnit2D = 60.0;
    const double scale = pixelsPerUnit2D * zoom_;
    if (scale <= 0.0) return;
    center2D_.x -= deltaX / scale;
    center2D_.y += deltaY / scale;
}

void Camera::pan3DByPixels(double deltaX, double deltaY) noexcept {
    const double scale = pixelsPerUnit_ * zoom_;
    if (scale <= 0.0) return;
    // Ekran x saga, y asagi artar. Icerik fareyle ayni yone kayar -> merkez
    // ters yone tasinir. Gorunum-uzayi delta -> dunya: R^T * delta.
    const double dx = -deltaX / scale, dy = deltaY / scale;
    center3D_ = center3D_ + Vec3{
        R_[0] * dx + R_[3] * dy,
        R_[1] * dx + R_[4] * dy,
        R_[2] * dx + R_[5] * dy};
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
    R_ = buildRotation(-0.55, 0.45, 0.0);
    zoom_ = 1.0;
    center2D_ = {};
    center3D_ = {};
}

void Camera::setView(StandardView view) noexcept {
    constexpr double halfPi = 1.57079632679489661923;
    constexpr double pi = 3.14159265358979323846;
    switch (view) {
    case StandardView::Isometric:
        // TEK TEMSIL: izometrik artik rotasyon matrisiyle kurulur. Ekran
        // satirlari eski isoM_ onizlemesiyle piksel birebir ayni:
        // row0=(0.707,-0.707,0), row1=(-0.408,-0.408,0.816); derinlik satiri
        // proper (det=+1). Euler ayrismasi YOK -> kutup devrilmesi/atlama yok.
        R_ = buildRotation(0.75 * pi, -0.6154797086703868, pi / 3.0 * 2.0);
        break;
    case StandardView::Top: R_ = buildRotation(0.0, 0.0, 0.0); break;
    case StandardView::Bottom: R_ = buildRotation(pi, 0.0, 0.0); break;
    case StandardView::Front: R_ = buildRotation(0.0, halfPi, 0.0); break;
    case StandardView::Back: R_ = buildRotation(0.0, -halfPi, 0.0); break;
    case StandardView::Left: R_ = buildRotation(halfPi, 0.0, 0.0); break;
    case StandardView::Right: R_ = buildRotation(-halfPi, 0.0, 0.0); break;
    }
}

double Camera::yaw() const noexcept {
    double y{}, p{}, r{};
    decompose(y, p, r);
    return y;
}

double Camera::pitch() const noexcept {
    double y{}, p{}, r{};
    decompose(y, p, r);
    return p;
}

double Camera::roll() const noexcept {
    double y{}, p{}, r{};
    decompose(y, p, r);
    return r;
}

double Camera::zoom() const noexcept { return zoom_; }
double Camera::pixelsPerUnit() const noexcept { return pixelsPerUnit_; }

// --- 3Dconnexion Navlib koprusu ---------------------------------------------
// cameraToWorld = R^T (m[row*4+col]; m[0..2]=R satir0, m[4..6]=satir1,
// m[8..10]=satir2), translasyon = center3D_ (navlib kamera pozisyonu).
std::array<double, 16> Camera::cameraToWorldMatrix4() const noexcept {
    std::array<double, 16> m{};
    m[0] = R_[0]; m[1] = R_[3]; m[2] = R_[6];  m[3] = 0.0;
    m[4] = R_[1]; m[5] = R_[4]; m[6] = R_[7];  m[7] = 0.0;
    m[8] = R_[2]; m[9] = R_[5]; m[10] = R_[8]; m[11] = 0.0;
    m[12] = center3D_.x; m[13] = center3D_.y; m[14] = center3D_.z; m[15] = 1.0;
    return m;
}

void Camera::applyCameraToWorldMatrix4(const std::array<double, 16>& matrix) noexcept {
    // cameraToWorld'nun rotasyonu R^T -> R = transpoze (R[i*3+j] = m[j*4+i]).
    std::array<double, 9> r{
        matrix[0], matrix[4], matrix[8],
        matrix[1], matrix[5], matrix[9],
        matrix[2], matrix[6], matrix[10]};
    orthonormalize(r);
    R_ = r;
    center3D_ = Vec3{matrix[12], matrix[13], matrix[14]};
}

} // namespace mm
