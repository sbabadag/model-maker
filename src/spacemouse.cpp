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
    // Navlib'in gonderdigi matris: m[0..8] rotasyon, m[12..14] KAMERA POZISYONU.
    // Bizim kamera hedef-tabanli (center3D_ + matris R_ + zoom_), o yuzden
    // pozisyonu dogrudan center3D_'ye yazamayiz (kavram farki). Dogal hareket
    // (kullanici tarifi):
    //   dort yone IT   = PAN (sag-sol: kamera right; ileri-geri: zeminde ileri)
    //   BAS (asagi)    = ZOOM OUT, CEK (yukari) = ZOOM IN
    //   bukme/cevirme  = ROTASYON (m[0..8] -> applyCameraToWorldMatrix4)
    std::array<double, 16> m{};
    for (int i = 0; i < 16; ++i) m[static_cast<std::size_t>(i)] = matrix[i];

    // --- DIAG: navlib'in gercek gonderdigi veri (ilk 400 cagri) ---
    {
        static int nDiag = 0;
        if (nDiag++ < 400) {
            FILE* f = fopen("model-maker-render.log", "a");
            if (f) {
                fprintf(f, "SM-SET-CAM #%d pos=%+.1f %+.1f %+.1f rot=[%+.2f %+.2f %+.2f|"
                           "%+.2f %+.2f %+.2f|%+.2f %+.2f %+.2f]\n",
                        nDiag, m[12], m[13], m[14],
                        m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);
                fclose(f);
            }
        }
    }

    const Vec3 oldCenter = camera_.center3D();

    // Rotasyonu coz (center3D_'ye dokunmadan once eski degeri tut).
    camera_.applyCameraToWorldMatrix4(m);
    camera_.setCenter3D(oldCenter); // pan'i manuel yapacagiz

    // Kameranin guncel right/up/forward vektorleri (rotasyonu uyguladiktan sonra).
    const auto cm = camera_.cameraToWorldMatrix4();
    const Vec3 right{cm[0], cm[1], cm[2]};
    const Vec3 up{cm[4], cm[5], cm[6]};
    const Vec3 fwd{-cm[8], -cm[9], -cm[10]}; // m[8..10] = backward

    // m[12..14] pozisyon delta'si (birikimsiz, her adim).
    static double prevX = 0.0, prevY = 0.0, prevZ = 0.0;
    static bool init = false;
    const double px = matrix[12], py = matrix[13], pz = matrix[14];
    if (!init) {
        prevX = px; prevY = py; prevZ = pz;
        init = true;
    } else {
        const double dx = px - prevX, dy = py - prevY, dz = pz - prevZ;
        prevX = px; prevY = py; prevZ = pz;
        const Vec3 d{dx, dy, dz};
        const double panR = d.x * right.x + d.y * right.y + d.z * right.z; // sag-sol it
        const double panU = d.x * up.x + d.y * up.y + d.z * up.z;          // basma/cekme
        const double dolly = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;      // ileri-geri it
        // PAN (dort yon): model itilen yone kayar -> merkez ters yone kayar.
        // Sag-sol: kamera right ekseni boyunca.
        const double panScale = 0.5; // hassaslik ayari
        Vec3 centerDelta{-right.x * panR, -right.y * panR, -right.z * panR};
        centerDelta = centerDelta * panScale;
        // Ileri-geri: forward'i zemine (XY duzlemine, Z-up) projekte et — bakis
        // acisi ne olursa olsun pano zeminde kayar (Tekla gorunumu).
        Vec3 groundFwd{fwd.x, fwd.y, 0.0};
        const double glen = std::sqrt(groundFwd.x * groundFwd.x + groundFwd.y * groundFwd.y);
        if (glen > 1e-6) {
            groundFwd = Vec3{groundFwd.x / glen, groundFwd.y / glen, 0.0};
            centerDelta = centerDelta +
                          Vec3{-groundFwd.x * dolly, -groundFwd.y * dolly, 0.0} * panScale;
        }
        camera_.setCenter3D(oldCenter + centerDelta);
        // ZOOM (dikey): CEK (yukari, panU>0) = zoom IN, BAS (asagi, panU<0) = zoom OUT.
        if (std::fabs(panU) > 1e-3) {
            double factor = 1.0 + panU / 10000.0; // model genisligi ~10m
            if (factor > 0.5 && factor < 2.0) camera_.zoomBy(factor);
        }
    }
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
    (void)plane;
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
    double z = camera_.zoom();
    if (z <= 0) z = 1.0;
    fov = 2.0 * std::atan(1.0 / z); // yakin ~0.9 rad (zoom 1.0)
    return 0;
}

long SpaceMouseNav::SetViewFOV(double fov) {
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
    // Perspektif raporla: kullanici bu modda dondurme + pan'in dogru calistigini
    // dogruladi. Ortho (0) raporlayinca navlib pan/zoom'u farkli (hizli) hesaplar
    // ve kamera-hedef kaymasi olur. Zoomu SetViewFOV uzerinden hallederiz.
    perspective = 1;
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
    (void)transform;
    return -1; // secim yok
}

long SpaceMouseNav::SetSelectionTransform(const navlib::matrix_t& matrix) {
    (void)matrix;
    return -1;
}

} // namespace mm
#endif // _WIN32
