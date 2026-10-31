// UCS DELTA DONUSUMU — dolgusal kopyala/tasima paneli ve "dx,dy,dz" girdisi
// IS DUZLEMI eksenlerindedir; geometri dunyadadir.
//
// Selahattin 2026-10-31: "kopyalama/move komutlari calisma duzlemi uzerinde
// calismiyor, hep globalde calisiyor". Tek kaynak: geometry.hpp
// workPlaneDeltaToWorld / worldDeltaToWorkPlane.
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol &&
           std::fabs(a.z - b.z) <= tol;
}
static double dot3(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static double len3(Vec3 a) { return std::sqrt(dot3(a, a)); }

int main() {
    // (1) DUNYA duzlemi: donusum kimlik.
    {
        const WorkPlane w = WorkPlane::world();
        const Vec3 d{100.0, -50.0, 25.0};
        check(near3(workPlaneDeltaToWorld(w, d), d), "dunya duzlemi: UCS delta = dunya delta");
        check(near3(worldDeltaToWorkPlane(w, d), d), "dunya duzlemi: ters donusum kimlik");
    }

    // (2) 45 derece egik is duzlemi.
    const auto opt = WorkPlane::fromThreePoints({0.0, 0.0, 0.0}, {1000.0, 0.0, 1000.0},
                                                {0.0, 1000.0, 0.0});
    check(opt.has_value(), "egik is duzlemi kuruldu");
    if (!opt) return 1;
    const WorkPlane& p = *opt;

    // (2a) Yalniz UCS X -> dunyada |d| korunur ve normal bileseni 0.
    {
        const Vec3 w = workPlaneDeltaToWorld(p, {100.0, 0.0, 0.0});
        std::printf("       UCS(100,0,0) -> dunya (%.3f %.3f %.3f)\n", w.x, w.y, w.z);
        check(std::fabs(len3(w) - 100.0) < 1e-9, "egik duzlem: UCS X uzunlugu korunur");
        check(std::fabs(dot3(w, p.normal)) < 1e-9, "egik duzlem: UCS X duzlem ICINDE kalir");
    }

    // (2b) Yalniz UCS Z (= duzlem normali) -> dunyada TAM normal yonunde.
    {
        const Vec3 w = workPlaneDeltaToWorld(p, {0.0, 0.0, 50.0});
        std::printf("       UCS(0,0,50) -> dunya (%.3f %.3f %.3f)\n", w.x, w.y, w.z);
        check(std::fabs(dot3(w, p.normal) - 50.0) < 1e-9,
              "egik duzlem: UCS Z = duzlem NORMALI boyunca");
        check(std::fabs(len3(w) - 50.0) < 1e-9, "egik duzlem: UCS Z uzunlugu korunur");
    }

    // (2c) Round-trip: dunya -> UCS -> dunya birebir.
    {
        const Vec3 world{137.0, -422.0, 88.5};
        const Vec3 ucs = worldDeltaToWorkPlane(p, world);
        const Vec3 back = workPlaneDeltaToWorld(p, ucs);
        std::printf("       round-trip geri donen: (%.3f %.3f %.3f)\n", back.x, back.y, back.z);
        check(near3(back, world, 1e-9), "egik duzlem: dunya -> UCS -> dunya birebir");
    }

    // (2d) Dunya Z'de 100 mm, egik duzlemde 3 bilesene ayrilir (global davranis degil).
    {
        const Vec3 ucs = worldDeltaToWorkPlane(p, {0.0, 0.0, 100.0});
        std::printf("       dunya(0,0,100) -> UCS (%.3f %.3f %.3f)\n", ucs.x, ucs.y, ucs.z);
        check(std::fabs(ucs.z - dot3(Vec3{0,0,100}, p.normal)) < 1e-9,
              "dunya Z deltasi UCS'te normal bilesenine ayrilir");
        check(std::fabs(ucs.x) > 1.0 || std::fabs(ucs.y) > 1.0,
              "dunya Z deltasi UCS'te duzlem-ici bilesen de uretir (global degil)");
    }

    std::printf("%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
