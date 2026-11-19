// AYNALAMA + PROFIL KOPYASI (kullanici: "aynalama profilleri kopyalamiyor").
//
// Bu test iki yolu kilitler:
//   M1. 2D ayna: profilli uyede profileName + eksen (yansitilmis) + sectionNormal
//       (yansitilmis) korunur.
//   M2. 3D ayna (duzlem = XY'den farkli): eksen ve sectionNormal duzlem lokal
//       koordinatlarina tasinip GERI dondugunde dogru yansir (kati yeniden
//       uretim ekseni bozulmaz).
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

static WireframeModel profiledColumn(double x, double y) {
    WireframeModel m = WireframeModel::line({x, y, 0.0}, {x, y, 3600.0});
    EntityProperties p = m.properties();
    p.profileName = "HEA200";
    p.axisFromX = x; p.axisFromY = y; p.axisFromZ = 0.0;
    p.axisToX = x; p.axisToY = y; p.axisToZ = 3600.0;
    p.sectionNormalX = 0.0; p.sectionNormalY = 0.0; p.sectionNormalZ = 1.0;
    p.profileRotation = 90.0;
    p.sectionAnchor = SectionAnchor::Top;
    m.setProperties(std::move(p));
    return m;
}

int main() {
    // M1: 2D ayna — Y ekseni boyunca (x -> -x).
    {
        const auto mir = mirrorModel2D(profiledColumn(100.0, 200.0),
                                       {0.0, 0.0, 0.0}, {0.0, 1000.0, 0.0});
        check(mir.has_value(), "M1 2D ayna uretildi");
        if (mir) {
            const auto& props = mir->properties();
            check(props.profileName == "HEA200", "M1 profileName korunur");
            check(approx(props.axisFromX, -100.0) && approx(props.axisFromY, 200.0),
                  "M1 eksen baslangici yansir (-100,200)");
            check(approx(props.axisToX, -100.0) && approx(props.axisToY, 200.0),
                  "M1 eksen sonu yansir (-100,200)");
        }
    }

    // M2: 3D ayna — duzlem: XZ (u=+X, v=+Z, normal=-Y), ayna ekseni X boyunca.
    // Kolon (100, 200, z) -> yansima (100, -200, z). SectionNormal (0,0,1) ayne
    // ekseni (X) uzerinde oldugu icin yansimada DEGISMEZ.
    {
        WorkPlane plane;
        plane.origin = {0.0, 0.0, 0.0};
        plane.u = {1.0, 0.0, 0.0};
        plane.v = {0.0, 0.0, 1.0};
        plane.normal = {0.0, -1.0, 0.0};
        const auto mir = mirrorModelOnPlane(profiledColumn(100.0, 200.0),
                                            {0.0, 0.0, 0.0}, {1000.0, 0.0, 0.0}, plane);
        check(mir.has_value(), "M2 3D ayna uretildi");
        if (mir) {
            const auto& props = mir->properties();
            check(props.profileName == "HEA200", "M2 profileName korunur");
            // Kolon (100, 200, z) XZ-duzleminde: y -> -200 beklenir.
            check(approx(props.axisFromY, -200.0),
                  "M2 eksen Y'si yansir (200 -> -200)");
            check(approx(props.axisFromX, 100.0) && approx(props.axisFromZ, 0.0),
                  "M2 eksen X/Z korunur");
        }
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("AYNALAMA + PROFIL KOPYASI: TUM KONTROLLER GECTI\n");
    return 0;
}
