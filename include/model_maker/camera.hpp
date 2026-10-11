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
    // DONDURME MERKEZI (R tusu): kayitli pivot etrafinda yorunge donusu.
    // Pivot dunya noktasi EKRANDA SABIT kalir: merkez3D pivot etrafinda
    // ayni W kadar doner ve R_ = R_*W yapilir; W = R^T*rx*R*ry oldugu icin
    // R_eski*(pivot-merkez3D_eski) = R_yeni*(pivot-merkez3D_yeni) — yani
    // pivot altindaki nokta piksel piksel yerinde kalir. Pivot yokken
    // rotate() birebir eski davranistir (pivot = merkez3D).
    void rotateAbout(const Vec3& pivot, double yawDelta, double pitchDelta) noexcept;
    void setOrbitPivot(const Vec3& worldPoint) noexcept { orbitPivot_ = worldPoint; }
    void clearOrbitPivot() noexcept { orbitPivot_.reset(); }
    bool hasOrbitPivot() const noexcept { return orbitPivot_.has_value(); }
    const std::optional<Vec3>& orbitPivot() const noexcept { return orbitPivot_; }
    // GRID GORUNUS DUZLEMI KILIDI. Kullanici sarti: "grid ... bir kez tek
    // duzlemde cizilir, goruntunun 3d degismesi grid in cizilimini
    // degistirmez". Gorunus yonu DUNYADA, gorunus tabani kurulurken BIR KEZ
    // saklanir (varsayilan yapici / reset / setView / setViewBasis); renderer
    // bu yonden gridViewPlaneForDirection ile XY/XZ/YZ secer. Sonradan yapilan
    // yorunge donusu (rotate/rotateAbout), pan ve zoom bunu DEGISTIRMEZ —
    // canli R_ KULLANILMAZ, yoksa 3B goruntuyu dondurmek grid cizimini
    // degistirirdi (45 derece esiklerinde aile sicramasi).
    const Vec3& gridViewDirection() const noexcept { return gridViewDirection_; }
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

    // --- VIEWCUBE + gecis animasyonu ----------------------------------------
    // Rotasyon matrisine dogrudan erisim. setRotationMatrix orthonormalize
    // eder; donus merkezini TEMIZLEMEZ ve grid kilidini YENILEMEZ — gecis
    // animasyonunun ARA kareleri bununla yazilir. Son kare normal apply
    // (setView/setViewBasis) ile biter; kilit/pivot orada kurulur.
    std::array<double, 9> rotationMatrix() const noexcept { return R_; }
    void setRotationMatrix(const std::array<double, 9>& rotation) noexcept;
    // Standart gorunusun world->camera matrisi — setView ile AYNI (tek kaynak).
    static std::array<double, 9> standardViewRotation(StandardView view) noexcept;
    // roll=0 "turntable": ekran saga = (cos yaw, 0, sin yaw); ekran yukari =
    // (sin pitch*sin yaw, cos pitch, -sin pitch*cos yaw). 6 standart gorunusun
    // TAMAMI bu aileye aittir; ViewCube'un 12 kenar + 8 kose yonu de ayni
    // aileden turetilir — boylece kenar/kose gorunusleri komsu yuzlerle
    // TUTARLI olur (ayri bir kural yok).
    static std::array<double, 9> turntableRotation(double yaw, double pitch) noexcept;
    // Keyfi gorunus tabani: ekran saga = right, ekran yukari = up (dunya
    // vektorleri; ortonormalize edilir), merkez = center. Iki noktali
    // gorunus (Tekla) bunu kullanir. Gecersiz (paralel/sifir) girdide no-op.
    void setViewBasis(const Vec3& right, const Vec3& up, const Vec3& center) noexcept;

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
    // R ile secilen donus merkezi. Kamera basina saklanir (viewport takasinda
    // kendiliginden tasinir); reset/setView/setViewBasis temizler, pan/zoom
    // korur (pivot bir DUNYA noktasidir, kameradan bagimsizdir).
    std::optional<Vec3> orbitPivot_{};
    // Kilitli gorunus yonu (dunya). Varsayilan = kimlik R'nin 3. satiri
    // (0,0,1) = ustten bakis -> plan (XY) gridi; reset()/setView()/
    // setViewBasis() hemen gercek tabana kilitler.
    Vec3 gridViewDirection_{0.0, 0.0, 1.0};

    static std::array<double, 9> buildRotation(double yaw, double pitch, double roll) noexcept;
    void decompose(double& yaw, double& pitch, double& roll) const noexcept;
    // R_'nin 3. satirini (= dunya bakis yonu) kilide yazar. YALNIZ gorunus
    // tabani kurulurken cagrilir; rotate/pan/zoom'da CAGRILMAZ.
    void lockGridViewDirection() noexcept;
};

} // namespace mm
