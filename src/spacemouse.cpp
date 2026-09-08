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

// --- IEvents ---------------------------------------------------------------
long SpaceMouseNav::SetActiveCommand(std::string commandId) {
    (void)commandId;
    return 0;
}

// --- ISpace3D ----------------------------------------------------------------
// Navlib Y-up: Y yukari, Z ekrandan disari, X saga. Bizim model Z-up (Z
// yukari). navlib.h: "non-identity matrix is REQUIRED when the ground plane is
// not the X-Z plane" (Lock Horizon). Donusum: app(x,y,z) -> navlib(x,z,-y),
// yani X etrafinda -90°. Bizim Z -> navlib Y, bizim Y -> navlib -Z.
long SpaceMouseNav::GetCoordinateSystem(navlib::matrix_t& matrix) const {
    // Z-up -> navlib Y-up: -90° X donusumu (app(x,y,z) -> navlib(x,z,-y)).
    // Bizim model Z-up (kolonlar Z'de yukari, zemin XY). Navlib Y-up varsayar;
    // Lock Horizon icin non-identity matris GEREKLI. Kamera matrisleri (camera
    // ToWorld / applyCameraToWorld) kendi Z-up cercevemizde tutarli; donusum
    // sadece navlib'e "up ekseni Z" diye bildirir.
    for (int i = 0; i < 16; ++i) matrix[i] = 0.0;
    matrix[0] = 1.0;   // m00  x' = x
    matrix[6] = 1.0;   // m12  y' = z
    matrix[9] = -1.0;  // m21  z' = -y
    matrix[15] = 1.0;  // m33
    return 0;
}

long SpaceMouseNav::GetFrontView(navlib::matrix_t& matrix) const {
    for (int i = 0; i < 16; ++i) matrix[i] = 0.0;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;
    return 0;
}

// --- IView ------------------------------------------------------------------
long SpaceMouseNav::GetCameraMatrix(navlib::matrix_t& matrix) const {
    const auto m4 = camera_.cameraToWorldMatrix4();
    // cameraToWorldMatrix4 (mm) row-major ve navlib camera-to-world beklentisi
    // ile ayni siradadir (m[row*4+col] = right/up/back/position).
    for (int i = 0; i < 16; ++i) matrix[i] = m4[static_cast<std::size_t>(i)];
    return 0;
}

long SpaceMouseNav::SetCameraMatrix(const navlib::matrix_t& matrix) {
    {
        static int n = 0;
        if (n++ < 12) {
            FILE* f = fopen("model-maker-render.log", "a");
            if (f) {
                fprintf(f, "SM-SET-CAM #%d m30=%.4f m31=%.4f m32=%.4f | m20=%.4f m21=%.4f m22=%.4f\n",
                        n, matrix[12], matrix[13], matrix[14], matrix[8], matrix[9], matrix[10]);
                fclose(f);
            }
        }
    }
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

long SpaceMouseNav::SetCameraTarget(const navlib::point_t& target) {
    camera_.setCenter3D(Vec3{target.x, target.y, target.z});
    if (viewChangedCallback_) viewChangedCallback_();
    return 0;
}

long SpaceMouseNav::GetPointerPosition(navlib::point_t& position) const {
    // Ekran merkezindeki zemin noktasi (0,0,0) — navlib pivot/odak icin kullanir.
    position = navlib::point_t{0.0, 0.0, 0.0};
    return 0;
}

long SpaceMouseNav::SetPointerPosition(const navlib::point_t& position) {
    (void)position;
    return 0;
}

long SpaceMouseNav::GetViewConstructionPlane(navlib::plane_t& plane) const {
    // SDK ornegi (3DxTraceNL) boyle yapar: "We haven't got a construction plane
    // that should be kept parallel to the viewport". Bir duzlem verirsek navlib
    // orthografik gorunumde rotasyonu o duzleme kilitler ve dogru calismaz.
    return navlib::make_result_code(navlib::navlib_errc::no_data_available);
}

long SpaceMouseNav::GetViewExtents(navlib::box_t& extents) const {
    // Orthografik gorunum alani: merkez center3D, genislik zoom'a bagli.
    // Navlib bu degeri zoom (view.extents) icin kullanir. zoom arttikca
    // gorunum alani kuculur (yakinlastirma).
    const auto& c = camera_.center3D();
    double z = camera_.zoom();
    if (z <= 0) z = 1.0;
    const double half = 5000.0 / z;
    extents.min = navlib::point_t{c.x - half, c.y - half, c.z - half};
    extents.max = navlib::point_t{c.x + half, c.y + half, c.z + half};
    return 0;
}

long SpaceMouseNav::SetViewExtents(const navlib::box_t& extents) {
    // Orthografik gorunumde Navlib zoom'u view.extents uzerinden yapar:
    // extents genisler/yazar, biz zoom carpanina ceviririz (genislik artar
    // -> uzaklasir / zoom azalir).
    double width = extents.max.x - extents.min.x;
    if (width > 0) {
        double targetZoom = 10000.0 / width; // baslangic extents ~10m
        double current = camera_.zoom();
        if (current > 0) camera_.zoomBy(targetZoom / current);
    }
    if (viewChangedCallback_) viewChangedCallback_();
    return 0;
}

long SpaceMouseNav::GetViewFocusDistance(double& distance) const {
    distance = 5000.0;
    return 0;
}

long SpaceMouseNav::GetViewFOV(double& fov) const {
    // Mevcut zoom'u fov (radyan) olarak yansit: zoom arttikca fov kuculur.
    // Navlib bu degeri zoom baslangici olarak alir.
    double z = camera_.zoom();
    if (z <= 0) z = 1.0;
    fov = 2.0 * std::atan(1.0 / z); // yakin ~0.9 rad (zoom 1.0)
    return 0;
}

long SpaceMouseNav::SetViewFOV(double fov) {
    {
        static int n = 0;
        if (n++ < 12) {
            FILE* f = fopen("model-maker-render.log", "a");
            if (f) {
                fprintf(f, "SM-SET-FOV #%d fov=%.4f\n", n, fov);
                fclose(f);
            }
        }
    }
    // Navlib perspektif gorunumde zoom'u fov uzerinden degistirir: fov kuculur
    // -> yakinla. fov'u (radyan) bizim zoom carpanina cevir. (fov/2) = atan(h/w)
    if (fov > 0.001) {
        double target = 1.0 / std::tan(fov * 0.5);
        double current = camera_.zoom();
        if (current > 0) camera_.zoomBy(target / current);
    }
    if (viewChangedCallback_) viewChangedCallback_();
    return 0;
}

long SpaceMouseNav::GetViewFrustum(navlib::frustum_t& frustum) const {
    frustum.left = -1.0; frustum.right = 1.0;
    frustum.bottom = -1.0; frustum.top = 1.0;
    frustum.nearVal = 0.1; frustum.farVal = 1'000'000.0;
    return 0;
}

long SpaceMouseNav::SetViewFrustum(const navlib::frustum_t& frustum) {
    (void)frustum;
    return 0;
}

long SpaceMouseNav::GetIsViewPerspective(navlib::bool_t& perspective) const {
    // Orthografik kamera (AutoCAD tarzi). Navlib zoom'u view.extents uzerinden
    // yapar (SetViewExtents). Pozisyon-matrisi (m30/m31/m32) ile zoom yapmasin
    // diye perspective=1 vermeyiz — orthografikte pozisyon kaymasi zoom degil.
    perspective = 0;
    return 0;
}

long SpaceMouseNav::GetIsViewRotatable(navlib::bool_t& isRotatable) const {
    isRotatable = 1;
    return 0;
}

// --- IPivot -----------------------------------------------------------------
long SpaceMouseNav::GetPivotPosition(navlib::point_t& position) const {
    const auto& c = camera_.center3D();
    position = navlib::point_t{c.x, c.y, c.z};
    return 0;
}

long SpaceMouseNav::IsUserPivot(navlib::bool_t& userPivot) const {
    userPivot = 0;
    return 0;
}

long SpaceMouseNav::SetPivotPosition(const navlib::point_t& position) {
    camera_.setCenter3D(Vec3{position.x, position.y, position.z});
    if (viewChangedCallback_) viewChangedCallback_();
    return 0;
}

long SpaceMouseNav::GetPivotVisible(navlib::bool_t& visible) const {
    visible = 0;
    return 0;
}

long SpaceMouseNav::SetPivotVisible(bool visible) {
    (void)visible;
    return 0;
}

// --- IHit -------------------------------------------------------------------
long SpaceMouseNav::GetHitLookAt(navlib::point_t& position) const {
    const auto& c = camera_.center3D();
    position = navlib::point_t{c.x, c.y, c.z};
    return 0;
}

long SpaceMouseNav::SetHitAperture(double aperture) {
    (void)aperture;
    return 0;
}

long SpaceMouseNav::SetHitDirection(const navlib::vector_t& direction) {
    (void)direction;
    return 0;
}

long SpaceMouseNav::SetHitLookFrom(const navlib::point_t& eye) {
    (void)eye;
    return 0;
}

long SpaceMouseNav::SetHitSelectionOnly(bool onlySelection) {
    (void)onlySelection;
    return 0;
}

// --- IModel -----------------------------------------------------------------
long SpaceMouseNav::GetUnitsToMeters(double& meters) const {
    meters = 0.001; // 1 birim = 1 mm
    return 0;
}

long SpaceMouseNav::GetFloorPlane(navlib::plane_t& floor) const {
    floor.n = navlib::vector_t{0.0, 0.0, 1.0};
    floor.d = 0.0;
    return 0;
}

long SpaceMouseNav::GetModelExtents(navlib::box_t& extents) const {
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
    return GetModelExtents(extents);
}

long SpaceMouseNav::GetIsSelectionEmpty(navlib::bool_t& empty) const {
    empty = 1;
    return 0;
}

long SpaceMouseNav::GetSelectionTransform(navlib::matrix_t& transform) const {
    return -1; // secim yok
}

long SpaceMouseNav::SetSelectionTransform(const navlib::matrix_t& matrix) {
    (void)matrix;
    return -1;
}

} // namespace mm
#endif // _WIN32
