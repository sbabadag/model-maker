#pragma once

#include "model_maker/camera.hpp"

#include <array>
#include <optional>

namespace mm {

struct CubePoint {
    int x{};
    int y{};
    bool operator==(const CubePoint&) const = default;
};

struct CubeRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

// VIEWCUBE HEDEFI — kullanicinin kup uzerinde tikladigi ozellik.
//   Face   : 6 yuz. Dogrudan StandardView'a eslenir; Ctrl+1..7 ile AYNI yone
//            gider (tek kaynak: Camera::standardViewRotation).
//   Edge   : 12 kenar. (dx,dy,dz) uclusunde TAM BIR bilesen sifirdir.
//   Corner : 8 kose. (dx,dy,dz) uclusunde hicbir bilesen sifir degildir.
//   Home   : ISO/ev dugmesi.
// Kenar/kose yonleri 6 yuzle AYNI "turntable" ailesindendir (roll=0), yani
// kenardan bakis iki komsu yuzun arasinda, tutarli bir aciya oturur — ayri
// bir oryantasyon kurali yoktur.
enum class ViewCubeTargetKind { None, Face, Edge, Corner, Home };

struct ViewCubeTarget {
    ViewCubeTargetKind kind{ViewCubeTargetKind::None};
    StandardView face{StandardView::Isometric}; // kind == Face
    int dx{};                                   // kind == Edge/Corner (her biri -1/0/1)
    int dy{};
    int dz{};
    bool operator==(const ViewCubeTarget&) const = default;
};

struct ViewCubeFace {
    StandardView view{StandardView::Front};
    std::array<CubePoint, 4> points{};
    bool visible{};
    double depth{};
};

struct ViewCubeLayout {
    int centerX{};
    int centerY{};
    std::array<CubePoint, 8> corners{};
    std::array<ViewCubeFace, 6> faces{};
    CubeRect bodyBounds{};
    CubeRect homeControl{};
    CubePoint axisOrigin{};
    CubePoint xAxis{};
    CubePoint yAxis{};
    CubePoint zAxis{};
    // --- 26 YON (modern kup) ---
    // faces[] ile ayni sirada; kenar/kose tablolari kendi sabit sirasinda.
    std::array<ViewCubeTarget, 6> faceTargets{};
    std::array<ViewCubeTarget, 12> edgeTargets{};
    std::array<CubePoint, 12> edgeMidpoints{};
    std::array<bool, 12> edgeVisible{};
    std::array<ViewCubeTarget, 8> cornerTargets{};
    std::array<bool, 8> cornerVisible{};
};

// 12 kenar yonunun SABIT tablosu: her satirda tam bir bilesen sifirdir; kenar
// yonu ayni zamanda ekrandaki kenar ORTA NOKTASININ kupsel koordinatidir
// (iki ucun ortalamasi = yon uclusu).
inline constexpr std::array<std::array<int, 3>, 12> kViewCubeEdgeDirections{{
    {{0, -1, -1}}, {{0, 1, -1}}, {{0, -1, 1}}, {{0, 1, 1}},   // X'e paralel
    {{-1, 0, -1}}, {{1, 0, -1}}, {{-1, 0, 1}}, {{1, 0, 1}},   // Y'e paralel
    {{-1, -1, 0}}, {{1, -1, 0}}, {{1, 1, 0}},  {{-1, 1, 0}}   // Z'ye paralel
}};

// Kup yarim-kenari (piksel). layout(), hitTest() ve renderer AYNI degeri
// kullanmali: yoksa tiklanan kenar cizilen kenardan kayar.
inline constexpr double kViewCubeScalePx = 31.0;

// Tiklama toleranslari (piksel). Kose daha belirgin oldugu icin once kose
// denenir; yuz merkezi ikisinin de disinda kalir (kup kenari ~62 px).
inline constexpr int kViewCubeCornerRadiusPx = 13;
inline constexpr int kViewCubeEdgeHalfWidthPx = 11;

// Hedefin birim yon vektoru (dunya; kupsel kose/kenar/yuze karsilik gelir).
Vec3 viewCubeTargetDirection(const ViewCubeTarget& target) noexcept;

// Hedefin roll=0 turntable aci cifti (radyan). 26 yonun tamami icin gecerli;
// Front/Back'te (kutup) yaw 0'a sabitlenir. Face/Home icin de calisir.
bool viewCubeTurntableAngles(const ViewCubeTarget& target, double& yaw, double& pitch) noexcept;

// Hedefin world->camera rotasyon matrisi: yuz/ISO -> standardViewRotation
// (birebir Ctrl+1..7), kenar/kose -> turntableRotation.
std::array<double, 9> viewCubeTargetRotation(const ViewCubeTarget& target) noexcept;

// Iki rotasyon arasinda KISA YOL kuresel dogrusal interpolasyon (quaternion
// slerp). t<=0 -> from, t>=1 -> to. Euler ara kareleri kullanilmaz (gimbal
// devrilmesi ve isaret atlamasi olmasin).
std::array<double, 9> viewCubeSlerpRotation(const std::array<double, 9>& from,
                                            const std::array<double, 9>& to, double t) noexcept;

class ViewCube {
public:
    static CubeRect hostBounds(int canvasWidth, int canvasHeight) noexcept;
    static ViewCubeLayout layout(int viewportWidth, const Camera& camera) noexcept;
    static std::optional<ViewCubeTarget> hitTest(int x, int y, int viewportWidth,
                                                 const Camera& camera) noexcept;
    static bool containsWidget(int x, int y, int viewportWidth) noexcept;
};

} // namespace mm
