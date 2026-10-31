// GORUNUS PENCERESI SNAP IZDUSUMU regresyon testi.
// Selahattin 2026-10-31: "yine plan goruntulerinde snap tutmuyor".
// Kok neden: gorunus penceresinde (viewDef_) snap noktasi IS DUZLEMINE
// (workPlane_) iz dusuruluyordu. Aktif UCS baska duzlemdeyse nokta EKRANDA
// kayiyor, kullanici "snap tutmuyor" goruyor. Dogru kural: nokta GORUNUS
// duzlemine iz dusurulur (ekran konumu degismez).
#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
    else std::printf("ok   %s\n", what);
}
double screenGap(const Camera& cam, int w, int h, const Vec3& a, const Vec3& b) {
    const Vec2 pa = cam.project(a, w, h);
    const Vec2 pb = cam.project(b, w, h);
    return std::hypot(pa.x - pb.x, pa.y - pb.y);
}
}  // namespace

int main() {
    const int W = 1200, H = 900;

    // ---- PLAN GORUNUSU (ustten): right=+X, up=+Y, normal=+Z ------------------
    Camera plan;
    plan.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{3000, 0, 4000});

    const Vec3 endpoint{0, 75, 4075};                // kiris ust flans kosesi (y=75)
    const WorkPlane viewPlane{{0, 0, 4000}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const WorkPlane staleUcs{{0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}};   // YZ benzeri eski UCS

    const Vec3 onView = viewPlane.projectPoint(endpoint);
    const Vec3 onUcs = staleUcs.projectPoint(endpoint);

    std::printf("plan: gorunus duzlemi sapma=%.6f px, eski UCS sapma=%.3f px\n",
                screenGap(plan, W, H, endpoint, onView),
                screenGap(plan, W, H, endpoint, onUcs));

    check(screenGap(plan, W, H, endpoint, onView) < 1e-6,
          "plan: gorunus duzlemine izdusum ekran konumunu DEGISTIRMEZ");
    check(screenGap(plan, W, H, endpoint, onUcs) > 1.0,
          "plan: baska duzlemdeki UCS noktayi ekranda KAYDIRIR (eski hata)");
    check(std::fabs(onView.z - 4000.0) < 1e-9,
          "plan: izdusum gorunus duzlemi kotunda (z=4000)");

    // ---- XZ YAN GORUNUS: right=+X, up=+Z, normal=+Y -------------------------
    Camera side;
    side.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{3000, 0, 4000});

    const Vec3 sideEndpoint{0, 500, 4075};
    const WorkPlane sidePlane{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    const WorkPlane worldXy{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};     // eski davranis

    const Vec3 sideOnView = sidePlane.projectPoint(sideEndpoint);
    const Vec3 sideOnWorld = worldXy.projectPoint(sideEndpoint);

    std::printf("yan : gorunus duzlemi sapma=%.6f px, dunya XY sapma=%.3f px\n",
                screenGap(side, W, H, sideEndpoint, sideOnView),
                screenGap(side, W, H, sideEndpoint, sideOnWorld));

    check(screenGap(side, W, H, sideEndpoint, sideOnView) < 1e-6,
          "yan: gorunus duzlemine izdusum ekran konumunu DEGISTIRMEZ");
    check(screenGap(side, W, H, sideEndpoint, sideOnWorld) > 1.0,
          "yan: dunya XY duzlemi noktayi ekranda KAYDIRIR (eski hata)");

    std::printf(failures == 0 ? "TUM TESTLER GECTI\n" : "%d TEST BASARISIZ\n", failures);
    return failures == 0 ? 0 : 1;
}
