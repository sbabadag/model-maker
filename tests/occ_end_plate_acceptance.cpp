// ALIN LEVHASI BAGLANTISI (Phase C1) — gercek OCCT kabul testi.
//
// Levha prizmasi hacmi + bulon grubu montaji + baglanti montaji (fuse).
// Linux'ta gercek OpenCASCADE ile derlenip calistirilir (MM_HAS_OCC).

#ifdef MM_HAS_OCC
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"

#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <cstdio>
#include <cmath>

using namespace mm;

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}
static bool approx(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static double volume(const TopoDS_Shape& s) {
    if (s.IsNull()) return -1.0;
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

static bool bbox(const TopoDS_Shape& s, double& x0, double& y0, double& z0,
                 double& x1, double& y1, double& z1) {
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    if (b.IsVoid()) return false;
    b.Get(x0, y0, z0, x1, y1, z1);
    return true;
}

int main() {
    // --- 1) Levha prizmasi: 240 x 340 x 12 (kiris kesit 200x300 + 20 pay) ----
    EndPlateParams p;
    p.plateThickness = 12.0;
    p.plateExtra = 20.0;
    p.boltCountX = 2;
    p.boltCountY = 2;
    p.boltEdge = 30.0;
    const EndPlatePlacement pl =
        computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);

    const TopoDS_Shape plate = buildEndPlateFromPlacement(pl);
    check(!plate.IsNull(), "levha uretildi");
    const double pv = volume(plate);
    check(approx(pv, 240.0 * 340.0 * 12.0, 1e-3), "levha hacmi 240*340*12");
    double bx0, by0, bz0, bx1, by1, bz1;
    check(bbox(plate, bx0, by0, bz0, bx1, by1, bz1), "levha bbox var");
    // Levha merkezi (6,0,0); X: 0..12, Y: -120..120, Z: -170..170.
    check(approx(bx0, 0.0, 1e-3) && approx(bx1, 12.0, 1e-3), "levha X 0..12");
    check(approx(by0, -120.0, 1e-3) && approx(by1, 120.0, 1e-3), "levha Y -120..120");
    check(approx(bz0, -170.0, 1e-3) && approx(bz1, 170.0, 1e-3), "levha Z -170..170");

    // --- 2) Bulon grubu montaji (4 bulon, grip 30mm) ------------------------
    const TopoDS_Shape bolts = buildBoltGroupAssemblyShape(pl.bolts, 30.0);
    check(!bolts.IsNull(), "bulon grubu montaji uretildi");
    const double bv = volume(bolts);
    check(bv > 0.0, "bulon montaji hacmi > 0");

    // --- 3) Baglanti montaji (levha + bulonlar fuse) ------------------------
    const TopoDS_Shape asmShape = buildEndPlateAssemblyShape(pl, 30.0);
    check(!asmShape.IsNull(), "baglanti montaji uretildi");
    const double av = volume(asmShape);
    check(av >= pv, "baglanti hacmi >= levha hacmi");
    // Montaj levhayi ICERMELI: bbox Y/Z levha ile ayni genislikte (bulonlar
    // X boyunca uzanir, Y/Z'de levha sinirlarini asmaz).
    double ax0, ay0, az0, ax1, ay1, az1;
    check(bbox(asmShape, ax0, ay0, az0, ax1, ay1, az1), "baglanti bbox var");
    check(ay0 <= -120.0 + 1e-3 && ay1 >= 120.0 - 1e-3, "baglanti Y levhayi kapsar");
    check(az0 <= -170.0 + 1e-3 && az1 >= 170.0 - 1e-3, "baglanti Z levhayi kapsar");
    // Bulonlar X boyunca uzandigi icin X araligi levhadan genis olmali (bas/somun).
    check(ax1 - ax0 > 12.0, "baglanti X levhadan genis (bas/somun)");

    // --- 4) 2x4 bulon dizilimi (Tekla end plate tipik) ----------------------
    EndPlateParams p4;
    p4.plateThickness = 12.0;
    p4.plateExtra = 20.0;
    p4.boltCountX = 2;
    p4.boltCountY = 4;
    p4.boltEdge = 30.0;
    const EndPlatePlacement pl4 =
        computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p4);
    check(pl4.bolts.countX == 2 && pl4.bolts.countY == 4, "2x4 bulon");
    const std::vector<Vec3> origins4 = boltGroupBoltOrigins(pl4.bolts);
    check(origins4.size() == 8, "8 bulon merkezi");
    const TopoDS_Shape bolts4 = buildBoltGroupAssemblyShape(pl4.bolts, 30.0);
    check(!bolts4.IsNull(), "2x4 bulon montaji uretildi");

    std::printf("occ_end_plate_acceptance: %d check, %d failure\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
#else
#include <cstdio>
int main() {
    std::printf("OCC devre disi — test atlandi\n");
    return 0;
}
#endif
