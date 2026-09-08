#pragma once

// 3Dconnexion Navlib (SDK 4.x) — SpaceMouse 6-DOF navigasyon koprusu.
// Bu SDK, eski si.h/SiOpen/SiGetEvent C API'sini KULLANMAZ. Bunun yerine
// uygulama bir IAccessors alt sinifini Navlib'e verir (CNavlibInterface),
// Navlib de SpaceMouse girdisiyle kamera-matrisi property'lerini
// (view.affine / view.target / motion_k ...) okuyup geri yazar. Yani:
//   GetCameraMatrix  -> mevcut kamerayi camera-to-world 4x4 olarak ver
//   SetCameraMatrix  -> Navlib yeni matrisi uretir, biz kameraya uygulariz
// Bu dosya YALNIZ _WIN32'de derlenir (SDK kutuphaneleri Windows-only).
// Linux'ta / NOSTALJI C API'sinde derlenmemeli.

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"

#ifdef _WIN32
#include <SpaceMouse/IAccessors.hpp>
#include <SpaceMouse/INavlib.hpp>
#include <SpaceMouse/CNavlibInterface.hpp>
#include <navlib/navlib.h>
#include <navlib/navlib_error.h>
#endif

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace mm {

#ifdef _WIN32
// Navlib, modelin koordinat sistemini (Y-up vs.) ve dunya birimini bu varlik
// uzerinden sorar. Biz milimetre kullaniyoruz (1 mm = 1 birim) ve Y-up bir
// bina grisi icin XY duzlemi zemini varsayar (Z dusey). Isometrik girdi icin
// coordinate_system matrix ile uyum saglanir.
class SpaceMouseNav final
    : public TDx::SpaceMouse::Navigation3D::IAccessors {
public:
    SpaceMouseNav(Camera& camera, Document& document);
    ~SpaceMouseNav() override;

    // Toplanti: Navlib baglantisi ac/kapa.
    // start(): CNavlibInterface olustur + Open("model-maker") — Navlib dll'si
    //          ve 3DxWare surucusu yoksa sessizce basarisiz (sistemi bozma).
    // stop():  Close() + bagimsiz birak.
    bool start();
    void stop();
    bool running() const noexcept { return running_; }

    // ISpace3D — koordinat sistemi / on gorunum yonu.
    long GetCoordinateSystem(navlib::matrix_t& matrix) const override;
    long GetFrontView(navlib::matrix_t& matrix) const override;

    // IView — kamera matrisi koprusu (KALP). Navlib bununla SpaceMouse'u
    // kameranin onunden arkasina tasir.
    long GetCameraMatrix(navlib::matrix_t& matrix) const override;
    long SetCameraMatrix(const navlib::matrix_t& matrix) override;
    long GetCameraTarget(navlib::point_t& target) const override;
    long SetCameraTarget(const navlib::point_t& target) override;
    long GetPointerPosition(navlib::point_t& position) const override;
    long SetPointerPosition(const navlib::point_t& position) override;
    long GetViewConstructionPlane(navlib::plane_t& plane) const override;
    long GetViewExtents(navlib::box_t& extents) const override;
    long SetViewExtents(const navlib::box_t& extents) override;
    long GetViewFocusDistance(double& distance) const override;
    long GetViewFOV(double& fov) const override;
    long SetViewFOV(double fov) override;
    long GetViewFrustum(navlib::frustum_t& frustum) const override;
    long SetViewFrustum(const navlib::frustum_t& frustum) override;
    long GetIsViewPerspective(navlib::bool_t& perspective) const override;
    long GetIsViewRotatable(navlib::bool_t& isRotatable) const override;

    // IModel — model extents / secim durumu.
    long GetModelExtents(navlib::box_t& extents) const override;
    long GetSelectionExtents(navlib::box_t& extents) const override;
    long GetSelectionTransform(navlib::matrix_t& transform) const override;
    long SetSelectionTransform(const navlib::matrix_t& matrix) override;
    long GetIsSelectionEmpty(navlib::bool_t& empty) const override;
    long GetUnitsToMeters(double& meters) const override;
    long GetFloorPlane(navlib::plane_t& floor) const override;

    // IPivot — dondurme pivo noktasi (Navlib bunu kameran ortasina koyar).
    long GetPivotPosition(navlib::point_t& position) const override;
    long IsUserPivot(navlib::bool_t& userPivot) const override;
    long SetPivotPosition(const navlib::point_t& position) override;
    long GetPivotVisible(navlib::bool_t& visible) const override;
    long SetPivotVisible(bool visible) override;

    // IHit — otomatik-pivot icin isik/isik-konisi hit testi. Simdilik hayir.
    long GetHitLookAt(navlib::point_t& position) const override;
    long SetHitAperture(double aperture) override;
    long SetHitDirection(const navlib::vector_t& direction) override;
    long SetHitLookFrom(const navlib::point_t& eye) override;
    long SetHitSelectionOnly(bool onlySelection) override;

    // IEvents — 3D Mouse butonlarindan uygulama komutlari.
    long SetActiveCommand(std::string commandId) override;
    long SetSettingsChanged(long count) override;
    long SetKeyPress(long vkey) override;
    long SetKeyRelease(long vkey) override;

    // IState — hareket basi/bitis bildirimi (animasyon dongusu).
    long SetTransaction(long transaction) override;
    long SetMotionFlag(bool motion) override;

private:
    // Uygulama, Navlib kamera degistirdiginde boyama cagrisi kurar.
    std::function<void()> viewChangedCallback_;

    Camera& camera_;
    Document& document_;
    std::unique_ptr<TDx::SpaceMouse::Navigation3D::CNavlibInterface> navlib_;
    bool running_{false};
    bool fromNavlib_{false}; // SetCameraMatrix -> uygulama geri cagrisi korsesi

public:
    // Uygulama boyama cagrisini Navlib'ten kamera degisimine baglar.
    void setViewChangedCallback(std::function<void()> cb) {
        viewChangedCallback_ = std::move(cb);
    }
#endif // _WIN32

} // namespace mm
