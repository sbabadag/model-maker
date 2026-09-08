#include "model_maker/spacemouse.hpp"

#ifdef _WIN32
#include "model_maker/document.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mm {

namespace nav3d = TDx::SpaceMouse::Navigation3D;

SpaceMouseNav::SpaceMouseNav(Camera& camera, Document& document)
    : nav3d::CNavigation3D(true /*multiThreaded*/, false /*rowVectors*/), camera_(camera),
      document_(document) {}

bool SpaceMouseNav::start() {
    if (IsEnabled()) return true;
    try {
        // CNavigation3D::EnableNavigation(true) -> m_pImpl->Open(profileHint)
        // -> NlCreate. Profil adi navlib'te model-maker olarak gozukur.
        // setProfileHint ile once profil metnini ver (Open bos metinle atar).
        PutProfileHint("model-maker");
        // Navlib'in bu baglantiyi 3D hedef olarak gormesi icin active/focus.
        // CNavigation3D bunlari kurmaz; biz Write ile bildiririz (navlib.h
        // geregi). Aksi halde cihaz 6 ekseni bu ornege yonlendirmez.
        EnableNavigation(true);
        Write(std::string("active"), navlib::value(true));
        Write(std::string("focus"), navlib::value(true));
    } catch (const std::exception&) {
        return false;
    }
    return IsEnabled();
}

void SpaceMouseNav::stop() {
    if (!IsEnabled()) return;
    EnableNavigation(false);
}

// --- ISpace3D --------------------------------------------------------------
// Navlib Y-up: Y yukari, Z ekrandan disari, X saga. Bizim model Z-up (Z
// yukari). navlib.h: "non-identity matrix is REQUIRED when the ground plane is
// not the X-Z plane" (Lock Horizon). Donusum: app(x,y,z) -> navlib(x,z,-y),
// yani X etrafinda -90°. Bizim Z -> navlib Y, bizim Y -> navlib -Z.
long SpaceMouseNav::GetCoordinateSystem(navlib::matrix_t& matrix) const {
    // matrix_t: m[row*4+col] row-major (operator[] ile). Donusum:
    //   row0: x' = x            -> m00=1
    //   row1: y' = z            -> m11=0, m12=1
    //   row2: z' = -y           -> m21=-1, m22=0
    for (int i = 0; i < 16; ++i) matrix[i] = 0.0;
    matrix[0] = 1.0;   // m00
    matrix[5] = 0.0;   // m11
    matrix[6] = 1.0;   // m12 (y' <- z)
    matrix[9] = -1.0;  // m21 (z' <- -y)
    matrix[10] = 0.0;  // m22
    matrix[15] = 1.0;  // m33
    return 0;
}

// Navlib on gorunumunu bizim koordinat sistemine cevirir. Identity yeterli
// (koordinat sistemi zaten -90° X ile cevrildi).
long SpaceMouseNav::GetFrontView(navlib::matrix_t& matrix) const {
    for (int i = 0; i < 16; ++i) matrix[i] = 0.0;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;
    return 0;
}

// --- IView ----------------------------------------------------------------
long SpaceMouseNav::GetCameraMatrix(navlib::matrix_t& matrix) const {
    const auto m4 = camera_.cameraToWorldMatrix4();
    // cameraToWorldMatrix4 (mm) row-major ve navlib camera-to-world beklentisi
    // ile ayni siradadir (m[row*4+col] = right/up/back/position).
    for (int i = 0; i < 16; ++i) matrix[i] = m4[static_cast<std::size_t>(i)];
    return 0;
}

long SpaceMouseNav::SetCameraMatrix(const navlib::matrix_t& matrix) {
    std::array<double, 16> m{};
    for (int i = 0; i < 16; ++i) m[static_cast<std::size_t>(i)] = matrix[i];
    camera_.applyCameraToWorldMatrix4(m);
    if (viewChangedCallback_) viewChangedCallback_();
    return 0;
}

long SpaceMouseNav::GetCameraTarget(navlib::point_t& target) const {
    const auto& c = camera_.center3D();
    target.x = c.x; target.y = c.y; target.z = c.z;
    return 0;
}

long SpaceMouseNav::GetViewFocusDistance(double& distance) const {
    distance = 5000.0;
    return 0;
}

long SpaceMouseNav::GetIsViewPerspective(navlib::bool_t& perspective) const {
    // Navlib rotasyonu perspektif gorunumde view.affine uzerinden yapar;
    // orthografik bildirince bazi surumler 6 eksen donusu kisitlar.
    perspective = 1;
    return 0;
}

long SpaceMouseNav::GetIsViewRotatable(navlib::bool_t& isRotatable) const {
    // 3D'de doner; 2B plan aktifken navlib baslatilmaz.
    isRotatable = 1;
    return 0;
}

long SpaceMouseNav::GetViewConstructionPlane(navlib::plane_t& plane) const {
    // Zemin duzlemi (z=0): normal (0,0,1), d=0.
    plane.n = navlib::vector_t{0.0, 0.0, 1.0};
    plane.d = 0.0;
    return 0;
}

long SpaceMouseNav::GetViewFOV(double& fov) const {
    fov = 0.5; // radyan — navlib 0 istemez (divide-by-zero korumasi).
    return 0;
}

long SpaceMouseNav::GetViewFrustum(navlib::frustum_t& frustum) const {
    frustum.left = -1.0; frustum.right = 1.0;
    frustum.bottom = -1.0; frustum.top = 1.0;
    frustum.nearVal = 0.1; frustum.farVal = 1'000'000.0;
    return 0;
}

// --- IModel ----------------------------------------------------------------
long SpaceMouseNav::GetUnitsToMeters(double& meters) const {
    meters = 0.001; // 1 birim = 1 mm -> 0.001 m
    return 0;
}

long SpaceMouseNav::GetModelExtents(navlib::box_t& extents) const {
    // Navlib, 6 ekseni model kapsamina gore olcekler; hata donerse navigasyonu
    // baslatmayabilir. Model yoksa bile gecerli kutu ver (10m varsayilan).
    auto b = document_.bounds();
    extents.min = navlib::point_t{b ? b->minimum.x : -5000.0,
                                  b ? b->minimum.y : -5000.0,
                                  b ? b->minimum.z : 0.0};
    extents.max = navlib::point_t{b ? b->maximum.x : 5000.0,
                                  b ? b->maximum.y : 5000.0,
                                  b ? b->maximum.z : 10000.0};
    return 0;
}

long SpaceMouseNav::GetSelectionExtents(navlib::box_t& extents) const {
    // Secim yok — model kapsamini ver (yoksa varsayilan).
    return GetModelExtents(extents);
}

} // namespace mm
#endif // _WIN32
