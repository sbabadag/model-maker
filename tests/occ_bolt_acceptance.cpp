// PHASE B1 KABUL — GERCEK OCCT (delik kesimi).
//
// Iki 10 mm levha, M20 bulon (delik capi 22) -> iki levhada da dogru eksende
// silindirik delik. Delik hacmi = pi*(r)^2*10 her levha icin (toplam 20 mm).
// Taban B-Rep DEGISMEZ (immutabilite); B-Rep gecerliligi (hacim > 0, sonlu,
// null degil) dogrulanir. Konum dogrulamasi: levha DISINA konan delik hicbir
// sey kesmez (silindir merkez-origin'e dogru yerlesmis demektir).
//
// Derleme (bu host, Linux OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_bolt_acceptance.cpp src/occ_geometry.cpp src/geometry.cpp \
//       src/cut_modifier.cpp src/bolt.cpp src/profile_database.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_bolt_acceptance && ./occ_bolt_acceptance

#include "model_maker/bolt.hpp"
#include "model_maker/occ_geometry.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near(double a, double b, double tol) {
    return std::abs(a - b) < tol;
}

int main() {
    const double r = 11.0;          // delik yaricapi (22/2)
    const double holeVol10 = M_PI * r * r * 10.0;  // bir levhada delinen hacim
    const double plateVol = 100.0 * 100.0 * 10.0;  // 100000 mm^3

    // Iki levha: plateA z=0..10, plateB z=10..20 (ust uste).
    const TopoDS_Shape plateA = BRepPrimAPI_MakeBox(100.0, 100.0, 10.0).Shape();
    const TopoDS_Shape plateB =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 10.0), 100.0, 100.0, 10.0).Shape();

    check(!plateA.IsNull() && !plateB.IsNull(), "iki levha olustu");
    check(near(shapeVolume(plateA), plateVol, 1.0), "plateA hacmi 100000");
    check(near(shapeVolume(plateB), plateVol, 1.0), "plateB hacmi 100000");

    // 1) Merkez deligi (50,50,10) +Z ekseni, cap 22 -> her iki levhayi deler.
    const CutModifier holeA =
        makeHoleCutModifier(1, {50.0, 50.0, 10.0}, {0.0, 0.0, 1.0}, 22.0);
    const TopoDS_Shape cutA = applyCutModifier(plateA, holeA, {}, {});
    check(!cutA.IsNull(), "delik sonucu null degil");
    check(near(shapeVolume(cutA), plateVol - holeVol10, 5.0),
          "plateA delik hacmi pi*11^2*10 cikarildi");

    // 2) Ayni delik plateB'de (eksen her iki levhayi gecer).
    const CutModifier holeB =
        makeHoleCutModifier(2, {50.0, 50.0, 10.0}, {0.0, 0.0, 1.0}, 22.0);
    const TopoDS_Shape cutB = applyCutModifier(plateB, holeB, {}, {});
    check(near(shapeVolume(cutB), plateVol - holeVol10, 5.0),
          "plateB delik hacmi pi*11^2*10 cikarildi");

    // 3) Taban B-Rep DEGISMEDI (immutabilite: girdi aynen kaldi).
    check(near(shapeVolume(plateA), plateVol, 1.0), "taban plateA korundu");
    check(near(shapeVolume(plateB), plateVol, 1.0), "taban plateB korundu");

    // 4) Levha DISINA delik -> hicbir sey kesmez (konum = origin, dunya orijini
    //    degil). Origin (200,200) 100x100 levhanin tamamen disinda.
    const CutModifier outside =
        makeHoleCutModifier(1, {200.0, 200.0, 10.0}, {0.0, 0.0, 1.0}, 22.0);
    const TopoDS_Shape noCut = applyCutModifier(plateA, outside, {}, {});
    check(near(shapeVolume(noCut), plateVol, 1.0),
          "levha disindaki delik kesmez (silindir origin'e yerlesti)");

    // 5) B-Rep gecerliligi: kesilmis sekil gecerli hacim > 0.
    check(shapeVolume(cutA) > 0.0 && std::isfinite(shapeVolume(cutA)),
          "kesilmis B-Rep gecerli hacim");

    // 6) BoltGroup -> iki parca icin iki delik; zincir uygulanir.
    BoltGroup bg;
    bg.parts = {1, 2};
    bg.origin = {50.0, 50.0, 10.0};
    bg.axis = {0.0, 0.0, 1.0};
    bg.params.holeDiameter = 22.0;
    const auto holes = boltGroupHoleModifiers(bg);
    check(holes.size() == 2, "bolt grubu 2 delik uretti");
    const TopoDS_Shape cutA2 = applyCutModifier(plateA, holes[0], {}, {});
    const TopoDS_Shape cutB2 = applyCutModifier(plateB, holes[1], {}, {});
    check(near(shapeVolume(cutA2) + shapeVolume(cutB2),
               2.0 * (plateVol - holeVol10), 10.0),
          "iki levha toplam delinen hacim dogru");

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
