#pragma once

// 3Dconnexion Navlib (SDK 4.x) — SpaceMouse 6-DOF navigasyon koprusu.
// Bu SDK eski si.h/SiOpen/SiGetEvent C API'sini KULLANMAZ. Uygulama,
// CNavigation3D taban sinifindan turer (SDK IAccessors'un tumunu CNavlibImpl
// ile halleder) ve sadece gorunum/metin metotlarini override eder. Navlib,
// SpaceMouse girdisiyle view.affine / view.target / motion_k property'lerini
// okuyup geri yazar:
//   GetCameraMatrix -> mevcut kamerayi camera-to-world 4x4 olarak ver
//   SetCameraMatrix -> Navlib yeni matrisi uretir, biz kameraya uygulariz
// Uygulama, SpaceMouse'u yalniz 3B gorunumde baslatir (2B planda etkisiz).
// YALNIZ _WIN32'de derlenir (SDK Windows-only). Navlib basliklarinin
// uygulama.hpp'e yayilmamasi icin burada yalniz forward-declare kullanilir.

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"

#ifdef _WIN32
#include <SpaceMouse/CNavigation3D.hpp>
#include <SpaceMouse/IAccessors.hpp>
#include <navlib/navlib.h>
#include <navlib/navlib_error.h>
#endif

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace mm {

#ifdef _WIN32
namespace nav3d = TDx::SpaceMouse::Navigation3D;
// CNavigation3D, IAccessors'in tamamini ve INavlibProperty (Write/Read)
// gerceklestirimini sunar; Navlib baglantisini CNavlibImpl ile kurar. Bizim
// tek isimiz: kamera + model kapsamini override edip uygulamanin kendi
// Camera/Document'ine baglamak (3DxTraceNL ornegindeki CNavigationModel gibi).
class SpaceMouseNav final : public nav3d::CNavigation3D {
public:
    SpaceMouseNav(Camera& camera, Document& document);
    ~SpaceMouseNav() override = default;

    // Baglanti ac/kapa. start(): CNavigation3D::EnableNavigation(true) —
    // Navlib dll + 3DxWare surucusu yoksa sessizce basarisiz.
    bool start();
    void stop();
    bool running() const noexcept { return IsEnabled(); }

    // IEvents
    long SetActiveCommand(std::string commandId) override;

    // ISpace3D — koordinat sistemi. Bizim model Z-up (kolonlar Z'de yukari,
    // zemin XY). Navlib Y-up varsayar; Lock Horizon icin non-identity matris.
    long GetCoordinateSystem(navlib::matrix_t& matrix) const override;
    long GetFrontView(navlib::matrix_t& matrix) const override;

    // IView — kamera matrisi koprusu (KALP).
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

    // IPivot
    long GetPivotPosition(navlib::point_t& position) const override;
    long IsUserPivot(navlib::bool_t& userPivot) const override;
    long SetPivotPosition(const navlib::point_t& position) override;
    long GetPivotVisible(navlib::bool_t& visible) const override;
    long SetPivotVisible(bool visible) override;

    // IHit
    long GetHitLookAt(navlib::point_t& position) const override;
    long SetHitAperture(double aperture) override;
    long SetHitDirection(const navlib::vector_t& direction) override;
    long SetHitLookFrom(const navlib::point_t& eye) override;
    long SetHitSelectionOnly(bool onlySelection) override;

    // IModel — model kapsami (Navlib 6 eksen olcegi).
    long GetUnitsToMeters(double& meters) const override;
    long GetFloorPlane(navlib::plane_t& floor) const override;
    long GetModelExtents(navlib::box_t& extents) const override;
    long GetSelectionExtents(navlib::box_t& extents) const override;
    long GetIsSelectionEmpty(navlib::bool_t& empty) const override;
    long GetSelectionTransform(navlib::matrix_t& transform) const override;
    long SetSelectionTransform(const navlib::matrix_t& matrix) override;

private:
    Camera& camera_;
    Document& document_;

public:
    // Uygulama boyama cagrisini Navlib'ten kamera degisimine baglar.
    void setViewChangedCallback(std::function<void()> cb) {
        viewChangedCallback_ = std::move(cb);
    }

private:
    std::function<void()> viewChangedCallback_;
};
#endif // _WIN32

} // namespace mm
