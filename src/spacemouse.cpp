#include "model_maker/spacemouse.hpp"

#ifdef _WIN32
#include "model_maker/document.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iterator>

namespace mm {

namespace nav3d = TDx::SpaceMouse::Navigation3D;

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
        // multiThreaded=false (tek-thread), rowMajor=true — kameranin
        // urettigi 4x4 matris row-major (m[row*4+col]) duzendedir; Navlib'e
        // bu bayrakla bildiririz ki m[]'yi dogru yorumlasin.
        navlib_.reset(new nav3d::CNavlibInterface(std::move(self), false, true));
        navlib_->Open("model-maker");
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
    // sistemin uygulama koordinatiyla ayni" der.
    std::fill(std::begin(matrix.m), std::end(matrix.m), 0.0);
    matrix.m00 = matrix.m11 = matrix.m22 = matrix.m33 = 1.0;
    return 0;
}

long SpaceMouseNav::GetFrontView(navlib::matrix_t& matrix) const {
    // On gorunum: Z ekseni bakisi (izometrik kamera). Kimlik 4x4 yeterli.
    std::fill(std::begin(matrix.m), std::end(matrix.m), 0.0);
    matrix.m00 = matrix.m11 = matrix.m22 = matrix.m33 = 1.0;
    return 0;
}

// --- IView: kamera matrisi koprusu (Navlib SpaceMouse'un kalbi) -------------
long SpaceMouseNav::GetCameraMatrix(navlib::matrix_t& matrix) const {
    const auto m4 = camera_.cameraToWorldMatrix4();
    // cameraToWorldMatrix4 row-major uretildi; navlib m[] alani m00..m33
    // sirali (m[row*4+col]). Navlib rowMajor=false ister ama biz CNavlibInterface
    // kurarken rowMajor=true verdik; navlib bu bayragla m[]'yi yorumlar.
    std::memcpy(matrix.m, m4.data(), sizeof(double) * 16);
    return 0;
}

long SpaceMouseNav::SetCameraMatrix(const navlib::matrix_t& matrix) {
    std::array<double, 16> m{};
    std::memcpy(m.data(), matrix.m, sizeof(double) * 16);
    fromNavlib_ = true;
    camera_.applyCameraToWorldMatrix4(m);
    fromNavlib_ = false;
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
    return 0;
}

long SpaceMouseNav::SetViewExtents(const navlib::box_t& extents) {
    (void)extents;
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
    auto b = document_.bounds();
    if (!b) {
        return navlib::make_result_code(static_cast<unsigned long>(navlib::navlib_errc::no_data_available));
    }
    extents.min = navlib::point_t{b->minimum.x, b->minimum.y, b->minimum.z};
    extents.max = navlib::point_t{b->maximum.x, b->maximum.y, b->maximum.z};
    return 0;
}

long SpaceMouseNav::GetSelectionExtents(navlib::box_t& extents) const {
    (void)extents;
    return navlib::make_result_code(static_cast<unsigned long>(navlib::navlib_errc::no_data_available));
}

long SpaceMouseNav::GetSelectionTransform(navlib::matrix_t& transform) const {
    std::fill(std::begin(transform.m), std::end(transform.m), 0.0);
    transform.m00 = transform.m11 = transform.m22 = transform.m33 = 1.0;
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
    (void)transaction;
    return 0;
}

long SpaceMouseNav::SetMotionFlag(bool motion) {
    (void)motion;
    return 0;
}

} // namespace mm
#endif // _WIN32
