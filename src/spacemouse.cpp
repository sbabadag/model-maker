#include "model_maker/spacemouse.hpp"

#ifdef _WIN32
#include "model_maker/document.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace mm {

namespace nav3d = TDx::SpaceMouse::Navigation3D;

void SpaceMouseNav::setIdentity(navlib::matrix_t& matrix) noexcept {
    for (int i = 0; i < 16; ++i) matrix[i] = 0.0;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;
}

SpaceMouseNav::SpaceMouseNav(Camera& camera, Document& document)
    : camera_(camera), document_(document) {}

SpaceMouseNav::~SpaceMouseNav() {
    stop();
}

bool SpaceMouseNav::start() {
    if (running_) return true;
    // CNavlibInterface, navlib.dll'nin NlCreate/NlClose/NlWriteValue
    // fonksiyonlarini kullanir (weak import). 3DxWare surucusu kuruluysa
    // navlib.dll PATH'te bulunur; kurulu degilse NlCreate yuklenemez ve
    // Open() istisna firlatir. Sessizce geri doner, uygulamayi bozmaz.
    //
    // ONEMLI: CNavlibInterface yalniz T_ == Navigation3D::IAccessors (ve
    // ActionInput::IActionAccessors) icin WeakAccessorPtr uzmanlasmasi tanir;
    // turetilmis (SpaceMouseNav) tur gecirilirse genel template kullanilir,
    // lock() nullptr dondurur ve Navlib hicbir accessor kaydedemez. Bu yuzden
    // shared_ptr<I> (baz) turunden geciyoruz — bize uretilen nesne unique_ptr
    // uyesi (spaceMouse_) olarak canli kalir, buradaki shared_ptr yalniz
    // constructor iletimindedir (weak_ptr destegi icin no-op deleter).
    std::shared_ptr<TDx::SpaceMouse::Navigation3D::IAccessors> self(
        this, [](TDx::SpaceMouse::Navigation3D::IAccessors*) {});
    try {
        // multiThreaded=true: Navlib property callback'leri (GetCameraMatrix/
        // SetCameraMatrix) Navlib'in kendi worker thread'inden gelir — bizim
        // Win32/Qt mesaj dongusunu pump etmemize gerek kalmaz. Isaretli tek
        // sey: cihaz degisiminde InvalidateRect thread-safe'dir. rowMajor=true
        // kameranin urettigi 4x4 matris row-major (m[row*4+col]) duzendedir.
        navlib_.reset(new nav3d::CNavlibInterface(std::move(self), true, true));
        navlib_->Open("model-maker");
        // Navlib, 3D Mouse girdisinin HANGI baglantiya gidecegini "active" ve
        // "focus" ozellikleriyle bilir. Open() bunlari ayarlamaz; biz kurariz,
        // yoksa cihaz 6 eksenini bu ornege yonlendirmez (NlCreate basarili ama
        // SM-SET-CAM hic gelmez). active=true → bu ornek 3D hedef, focus=true
        // → klavye odagi (dolayli odak yolu). Tek ornekli uygulama icin de sart.
        navlib_->Write(std::string("active"), navlib::value(true));
        navlib_->Write(std::string("focus"), navlib::value(true));
    } catch (const std::exception&) {
        navlib_.reset();
        return false;
    }
    running_ = true;
    return true;
}

void SpaceMouseNav::stop() {
    if (!running_) return;
    running_ = false;
    if (navlib_) navlib_->Close();
    navlib_.reset();
}

// --- ISpace3D --------------------------------------------------------------
long SpaceMouseNav::GetCoordinateSystem(navlib::matrix_t& matrix) const {
    // Kimlik (identity). Navlib Y-up bekler; bizim XY plan dogal olarak
    // Y-up koordinat sistemine yakindir. Bu matris Navlib'e "kendi koordinat
    // sistemin uygulama koordinatiyla ayni" der. matrix_t bireysel m00..m33
    // uyeleri + operator[] index erisimine sahiptir (.m dizi yok).
    setIdentity(matrix);
    return 0;
}

long SpaceMouseNav::GetFrontView(navlib::matrix_t& matrix) const {
    // On gorunum: Z ekseni bakisi (izometrik kamera). Kimlik 4x4 yeterli.
    setIdentity(matrix);
    return 0;
}

// --- IView: kamera matrisi koprusu (Navlib SpaceMouse'un kalbi) -------------
long SpaceMouseNav::GetCameraMatrix(navlib::matrix_t& matrix) const {
    const auto m4 = camera_.cameraToWorldMatrix4();
    // cameraToWorldMatrix4 row-major uretildi (m[row*4+col]); navlib matrix_t
    // operator[] index erisimi ayni siralamaya sahiptir (0..15 -> m00..m33).
    for (int i = 0; i < 16; ++i) matrix[i] = m4[static_cast<std::size_t>(i)];
    {
        static int n = 0; if (n++ < 4) {
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "SM-GET-CAM #%d\n", n); fclose(diag); }
        }
    }
    return 0;
}

long SpaceMouseNav::SetCameraMatrix(const navlib::matrix_t& matrix) {
    std::array<double, 16> m{};
    for (int i = 0; i < 16; ++i) m[static_cast<std::size_t>(i)] = matrix[i];
    fromNavlib_ = true;
    camera_.applyCameraToWorldMatrix4(m);
    fromNavlib_ = false;
    if (viewChangedCallback_) viewChangedCallback_();
    {
        static int n = 0; if (n++ < 4) {
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "SM-SET-CAM #%d yaw=%.3f pitch=%.3f c=(%.0f,%.0f,%.0f)\n",
                n, camera_.yaw(), camera_.pitch(), camera_.center3D().x,
                camera_.center3D().y, camera_.center3D().z); fclose(diag); }
        }
    }
    return 0;
}

long SpaceMouseNav::GetCameraTarget(navlib::point_t& target) const {
    const auto& c = camera_.center3D();
    target.x = c.x; target.y = c.y; target.z = c.z;
    {
        static int n = 0; if (n++ < 4) {
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "SM-GET-TARGET #%d\n", n); fclose(diag); }
        }
    }
    return 0;
}

long SpaceMouseNav::SetCameraTarget(const navlib::point_t& target) {
    camera_.setCenter3D(Vec3{target.x, target.y, target.z});
    if (viewChangedCallback_) viewChangedCallback_();
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "SM-SET-TARGET (%.0f,%.0f,%.0f)\n",
            target.x, target.y, target.z); fclose(diag); }
    }
    return 0;
}

long SpaceMouseNav::GetPointerPosition(navlib::point_t& position) const {
    // Imlec kenar duzleminde — imlec koordinati yoksa kameranin hedefinde
    // varsay (Navlib imlec-odakli pivot kullanirsa duzgun davranir).
    const auto& c = camera_.center3D();
    position.x = c.x; position.y = c.y; position.z = c.z;
    return 0;
}

long SpaceMouseNav::SetPointerPosition(const navlib::point_t& position) {
    (void)position;
    return 0;
}

long SpaceMouseNav::GetViewConstructionPlane(navlib::plane_t& plane) const {
    // Zemin duzlemi (z=0): normal (0,0,1), d=0.
    plane.n = navlib::vector_t{0.0, 0.0, 1.0};
    plane.d = 0.0;
    return 0;
}

long SpaceMouseNav::GetViewExtents(navlib::box_t& extents) const {
    double vw = camera_.pixelsPerUnit() * camera_.zoom();
    if (vw <= 0) vw = 1.0;
    // Tahmini gorunum alani (kamera cevirisinden bagimsiz genislik).
    const double cx = camera_.center3D().x, cy = camera_.center3D().y, cz = camera_.center3D().z;
    extents.min = navlib::point_t{cx - 5000.0, cy - 5000.0, cz - 5000.0};
    extents.max = navlib::point_t{cx + 5000.0, cy + 5000.0, cz + 5000.0};
    {
        static int n = 0; if (n++ < 4) {
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "SM-GET-EXTENTS #%d\n", n); fclose(diag); }
        }
    }
    return 0;
}

long SpaceMouseNav::SetViewExtents(const navlib::box_t& extents) {
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "SM-SET-EXTENTS (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)\n",
            extents.min.x, extents.min.y, extents.min.z,
            extents.max.x, extents.max.y, extents.max.z); fclose(diag); }
    }
    return 0;
}

long SpaceMouseNav::GetViewFocusDistance(double& distance) const {
    distance = 5000.0;
    return 0;
}

long SpaceMouseNav::GetViewFOV(double& fov) const {
    fov = 0.5; // radyan (~28.6°) — kameramiz orthografik olmasina ragmen
               // Navlib 0 istemez (divide-by-zero korumasi).
    return 0;
}

long SpaceMouseNav::SetViewFOV(double fov) {
    (void)fov;
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
    perspective = 0; // orthografik
    return 0;
}

long SpaceMouseNav::GetIsViewRotatable(navlib::bool_t& isRotatable) const {
    isRotatable = 1; // 3D'de doner (2B plan aktifken Navlib baslatilmaz)
    return 0;
}

// --- IModel ----------------------------------------------------------------
long SpaceMouseNav::GetModelExtents(navlib::box_t& extents) const {
    // Navlib, 6 ekseni modelin kapsam alani bilgisiyle olcekler. Hata
    // dondurursek navigasyonu baslatmayabilir; bu yuzden model yoksa bile
    // her zaman gecerli bir kutu veririz (grid ya da 10m varsayilan).
    auto b = document_.bounds();
    const double x0 = b ? b->minimum.x : -5000.0;
    const double y0 = b ? b->minimum.y : -5000.0;
    const double z0 = b ? b->minimum.z : 0.0;
    const double x1 = b ? b->maximum.x : 5000.0;
    const double y1 = b ? b->maximum.y : 5000.0;
    const double z1 = b ? b->maximum.z : 10000.0;
    extents.min = navlib::point_t{x0, y0, z0};
    extents.max = navlib::point_t{x1, y1, z1};
    return 0;
}

long SpaceMouseNav::GetSelectionExtents(navlib::box_t& extents) const {
    // Secim yok — Navlib'e bos kutu degil, model kapsami ver (yoksa varsayilan).
    return GetModelExtents(extents);
}

long SpaceMouseNav::GetSelectionTransform(navlib::matrix_t& transform) const {
    setIdentity(transform);
    return 0;
}

long SpaceMouseNav::SetSelectionTransform(const navlib::matrix_t& matrix) {
    (void)matrix;
    return 0;
}

long SpaceMouseNav::GetIsSelectionEmpty(navlib::bool_t& empty) const {
    empty = 1;
    return 0;
}

long SpaceMouseNav::GetUnitsToMeters(double& meters) const {
    meters = 0.001; // 1 birim = 1 mm -> 0.001 m
    return 0;
}

long SpaceMouseNav::GetFloorPlane(navlib::plane_t& floor) const {
    floor.n = navlib::vector_t{0.0, 0.0, 1.0};
    floor.d = 0.0;
    return 0;
}

// --- IPivot ----------------------------------------------------------------
long SpaceMouseNav::GetPivotPosition(navlib::point_t& position) const {
    const auto& c = camera_.center3D();
    position.x = c.x; position.y = c.y; position.z = c.z;
    return 0;
}

long SpaceMouseNav::IsUserPivot(navlib::bool_t& userPivot) const {
    userPivot = 0;
    return 0;
}

long SpaceMouseNav::SetPivotPosition(const navlib::point_t& position) {
    camera_.setOrbitCenter(Vec3{position.x, position.y, position.z});
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

// --- IHit (basit: her isik duzleme cakar) ----------------------------------
long SpaceMouseNav::GetHitLookAt(navlib::point_t& position) const {
    const auto& c = camera_.center3D();
    position.x = c.x; position.y = c.y; position.z = c.z;
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

// --- IEvents ---------------------------------------------------------------
long SpaceMouseNav::SetActiveCommand(std::string commandId) {
    (void)commandId;
    return 0;
}

long SpaceMouseNav::SetSettingsChanged(long count) {
    (void)count;
    return 0;
}

long SpaceMouseNav::SetKeyPress(long vkey) {
    (void)vkey;
    return 0;
}

long SpaceMouseNav::SetKeyRelease(long vkey) {
    (void)vkey;
    return 0;
}

// --- IState ----------------------------------------------------------------
long SpaceMouseNav::SetTransaction(long transaction) {
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "SM-TXN txn=%ld\n", transaction); fclose(diag); }
    }
    return 0;
}

long SpaceMouseNav::SetMotionFlag(bool motion) {
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "SM-MOTION m=%d\n", motion ? 1 : 0); fclose(diag); }
    }
    return 0;
}

} // namespace mm
#endif // _WIN32
