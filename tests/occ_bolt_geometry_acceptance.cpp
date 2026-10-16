// PHASE B1 SLICE 2 KABUL — GERCEK OCCT (bulon montaj geometrisi).
//
// M20 bulon (grip=20mm): altigen bas + silindirik govde + altigen somun + iki
// pul. Dogrular: B-Rep gecerliligi, eksen boyunca uzunluk (eksen konumu),
// eksene dik yari-cap, bilesen bayraklari (hasHead/hasNut/hasWasher), dejenere
// eksen -> null, standart boyut katalogu.
//
// Derleme:
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_bolt_geometry_acceptance.cpp src/bolt_geometry.cpp \
//       src/bolt.cpp src/geometry.cpp src/cut_modifier.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_bolt_geometry_acceptance && ./occ_bolt_geometry_acceptance

#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/occ_geometry.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool near(double a, double b, double tol) { return std::abs(a - b) < tol; }

// Eksen boyunca (birim `axis`) seklin izdusumunu [min,max] olarak dondurur.
static void axialExtent(const TopoDS_Shape& s, const Vec3& axis, double& mn,
                        double& mx) {
    Bnd_Box bb;
    BRepBndLib::Add(s, bb);
    double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    bb.Get(x0, y0, z0, x1, y1, z1);
    const double len =
        std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    const double ax = axis.x / len, ay = axis.y / len, az = axis.z / len;
    mn = +1e30;
    mx = -1e30;
    for (int i = 0; i < 8; ++i) {
        const double px = (i & 1) ? x1 : x0;
        const double py = (i & 2) ? y1 : y0;
        const double pz = (i & 4) ? z1 : z0;
        const double t = px * ax + py * ay + pz * az;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }
}

int main() {
    // M20: d=20 -> k=14 (bas), m=16 (somun), t=3 (pul), D=40 (pul dis).
    // grip=20 -> halfGrip=10. Eksen = +Z, origin = orijin.
    BoltGroup bg;
    bg.origin = {0.0, 0.0, 0.0};
    bg.axis = {0.0, 0.0, 1.0};
    bg.params.nominalDiameter = 20.0;
    bg.params.holeDiameter = 22.0;
    bg.params.grade = "8.8";

    const TopoDS_Shape asmShape = buildBoltAssemblyShape(bg, 20.0);
    check(!asmShape.IsNull(), "montaj null degil");
    check(std::isfinite(shapeVolume(asmShape)) && shapeVolume(asmShape) > 0.0,
          "montaj gecerli hacim");

    // Beklenen hacim (bilesenler ayrik — waster'lar govdeyle bitisik, cakisik
    // degil): head(hex) + nut(hex) + shank(cyl) + 2*washer(disc).
    const double sHead = 1.5 * 20.0;          // 30
    const double sNut = 1.5 * 20.0;           // 30
    const double headH = 0.7 * 20.0;          // 14
    const double nutH = 0.8 * 20.0;           // 16
    const double washerD = 2.0 * 20.0;        // 40
    const double washerT = 0.15 * 20.0;       // 3
    const double hexArea = 0.5 * std::sqrt(3.0) * sHead * sHead;
    const double expHead = hexArea * headH;
    const double expNut = hexArea * nutH;
    const double expShank = M_PI * 100.0 * 20.0;
    const double expWasher = M_PI * (washerD * 0.5) * (washerD * 0.5) * washerT;
    const double expTotal = expHead + expNut + expShank + 2.0 * expWasher;
    check(near(shapeVolume(asmShape), expTotal, 200.0), "montaj hacmi bilesenler toplami");

    // Eksen boyunca uzunluk: [-10-3-14, +10+3+16] = [-27, +29] -> uzunluk 56.
    double mn = 0, mx = 0;
    axialExtent(asmShape, bg.axis, mn, mx);
    check(near(mn, -27.0, 0.5) && near(mx, 29.0, 0.5),
          "eksen boyunca uclari dogru (bas -27, somun +29)");

    // Eksene dik yari-cap: washer dis cap 40 (en genis) -> yari 20.
    Bnd_Box bb;
    BRepBndLib::Add(asmShape, bb);
    double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    bb.Get(x0, y0, z0, x1, y1, z1);
    check(near(x0, -20.0, 0.5) && near(x1, 20.0, 0.5) && near(y0, -20.0, 0.5) &&
              near(y1, 20.0, 0.5),
          "eksene dik yari-cap ~20 (washer)");

    // Bilesen bayraklari: bas/somun/pul yoksa hacim azalir.
    BoltGroup noHead = bg;
    noHead.params.hasHead = false;
    noHead.params.hasNut = false;
    noHead.params.hasWasher = false;
    const TopoDS_Shape shankOnly = buildBoltAssemblyShape(noHead, 20.0);
    check(!shankOnly.IsNull(), "sadece govde null degil");
    check(near(shapeVolume(shankOnly), expShank, 50.0), "sadece govde hacmi");

    // Dejenere eksen -> null.
    BoltGroup badAxis = bg;
    badAxis.axis = {0.0, 0.0, 0.0};
    check(buildBoltAssemblyShape(badAxis, 20.0).IsNull(), "dejenere eksen null sekil");

    // Degisen grip uzunlugu eksen uclarini kaydirir.
    const TopoDS_Shape grip40 = buildBoltAssemblyShape(bg, 40.0);
    axialExtent(grip40, bg.axis, mn, mx);
    check(near(mn, -37.0, 0.5) && near(mx, 39.0, 0.5),
          "grip 40mm -> eksen uclari -37..+39");

    // Standart boyut katalogu.
    const auto& sizes = standardBoltDiameters();
    check(sizes.size() == 6 && near(sizes[0], 12.0, 1e-9) &&
              near(sizes[5], 30.0, 1e-9),
          "standart caplar M12..M30 (6 adet)");
    check(near(standardBoltHoleDiameter(12.0), 14.0, 1e-9) &&
              near(standardBoltHoleDiameter(20.0), 22.0, 1e-9) &&
              near(standardBoltHoleDiameter(30.0), 33.0, 1e-9) &&
              near(standardBoltHoleDiameter(10.0), 12.0, 1e-9),
          "standart bosluklu delik caplari");
    check(isBoltGradeValid("8.8") && isBoltGradeValid("10.9") &&
              !isBoltGradeValid("4.6") && isBoltGradeValid(""),
          "kalite dogrulama (8.8/10.9)");

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
