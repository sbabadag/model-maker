// P2 KABUL SENARYOSU — GERCEK OCCT dogrulama (Polygon Cut + Line Cut).
//
// Tekla semantigi:
//   Polygon Cut  : kapali cokgen, cokgen duzlemine DIK yonde through cut
//                  (derinlik otomatik, parca yuzunun 3mm disina uzatilir).
//   Line Cut     : iki nokta arasinda cizgi; cizgiyi iceren ve cizginin
//                  cizildigi (gorunum) duzlemine DIK kesme duzlemi; taraf
//                  secilir (keepPositive).
//
// 100x100x100 kutu ile KESIN hacim oranlari dogrulanir:
//   1) ic kare polygon (20..40 x 20..40) -> 20x20x100 sutun cikarilir (~0.96)
//   2) kenar kare polygon (80..120 x 20..80) -> kesisim cikarilir (~0.88)
//   3) line cut keepPositive=true  -> x>50 kalir (~0.50)
//   4) line cut keepPositive=false -> x<50 kalir (~0.50)
//   5) dejenere polygon (<3 nokta) -> base DOKUNULMAZ
//   6) collinear polygon (alan ~0) -> base DOKUNULMAZ
//   7) line cut dejenere (cizgi ∥ ref normal) -> base DOKUNULMAZ
//   8) HEB300 polygon cut -> hacim < taban (nitel)
//   9) taban immutabilite (tum islemlerden sonra base degismedi)
//
// Derleme (bu host, Linux OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_polygon_cut_acceptance.cpp src/occ_geometry.cpp src/geometry.cpp \
//       src/cut_modifier.cpp src/profile_database.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_polygon_cut_acceptance && ./occ_polygon_cut_acceptance

#include "model_maker/cut_modifier.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

int main() {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Shape();
    const double boxVol = shapeVolume(box);
    check(std::abs(boxVol - 1.0e6) < 1.0, "100x100x100 kutu hacmi ~1e6 mm^3");

    const Vec3 axisFrom{0.0, 0.0, 0.0};
    const Vec3 axisTo{100.0, 0.0, 0.0};

    // --- 1) Ic kare polygon (through cut, tam sutun). ---
    {
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        poly.polygon = {{20, 20, 0}, {40, 20, 0}, {40, 40, 0}, {20, 40, 0}};
        const TopoDS_Shape cut = applyCutModifier(box, poly, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        // 20x20x100 = 40000 cikarilir -> 960000 kalir.
        check(std::abs(ratio - 0.96) < 0.01, "ic kare polygon cut: ~0.96 kati kalir");
    }

    // --- 2) Kenar kare polygon (prisma kutunun disina tasar; kesisim cikarilir). ---
    {
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        poly.polygon = {{80, 20, 0}, {120, 20, 0}, {120, 80, 0}, {80, 80, 0}};
        const TopoDS_Shape cut = applyCutModifier(box, poly, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        // Kesisim: x[80,100] x y[20,80] x z[0,100] = 20*60*100 = 120000 -> 0.88.
        check(std::abs(ratio - 0.88) < 0.01, "kenar kare polygon cut: ~0.88 kati kalir");
    }

    // --- 2b) Cokgen duzlemi parcadan UZAK (z=-500) -> through cut yine calisir. ---
    {
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        poly.polygon = {{20, 20, -500}, {40, 20, -500}, {40, 40, -500}, {20, 40, -500}};
        const TopoDS_Shape cut = applyCutModifier(box, poly, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        check(std::abs(ratio - 0.96) < 0.01, "uzak polygon duzlemi: ~0.96 kati kalir");
    }

    // --- 3) Line cut keepPositive=true: cizgi (50,0,0)-(50,100,0), ref normal +Z. ---
    {
        CutModifier line;
        line.kind = CutKind::LineCut;
        line.polygon = {{50, 0, 0}, {50, 100, 0}};  // Y yonunde cizgi (XY duzleminde)
        line.planeNormal = {0, 0, 1};               // cizgi duzlemi normali (+Z)
        line.keepPositive = true;                   // normal +X tarafi (x>50) korunur
        const TopoDS_Shape cut = applyCutModifier(box, line, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        check(std::abs(ratio - 0.50) < 0.01, "line cut keepPositive: ~0.50 kati kalir (x>50)");
    }

    // --- 4) Line cut keepPositive=false -> x<50 korunur. ---
    {
        CutModifier line;
        line.kind = CutKind::LineCut;
        line.polygon = {{50, 0, 0}, {50, 100, 0}};
        line.planeNormal = {0, 0, 1};
        line.keepPositive = false;
        const TopoDS_Shape cut = applyCutModifier(box, line, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        check(std::abs(ratio - 0.50) < 0.01, "line cut keepNegative: ~0.50 kati kalir (x<50)");
    }

    // --- 4b) Line cut invertSide (P8) -> keepPositive terslenir. ---
    {
        CutModifier line;
        line.kind = CutKind::LineCut;
        line.polygon = {{50, 0, 0}, {50, 100, 0}};
        line.planeNormal = {0, 0, 1};
        line.keepPositive = true;   // normal +X (x>50) korunur
        line.invertSide = true;     // P8: ters -> x<50 korunur
        const TopoDS_Shape cut = applyCutModifier(box, line, axisFrom, axisTo);
        const double ratio = shapeVolume(cut) / boxVol;
        check(std::abs(ratio - 0.50) < 0.01, "line cut invertSide: ~0.50 kati kalir (x<50)");
    }

    // --- 5) Dejenere polygon (<3 nokta) -> base dokunulmaz. ---
    {
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        poly.polygon = {{20, 20, 0}, {40, 40, 0}};  // 2 nokta
        const TopoDS_Shape cut = applyCutModifier(box, poly, axisFrom, axisTo);
        check(std::abs(shapeVolume(cut) - boxVol) < 1e-3, "dejenere polygon (<3): base degismedi");
    }

    // --- 6) Collinear polygon (alan ~0) -> base dokunulmaz. ---
    {
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        poly.polygon = {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}};  // ayni dogru uzerinde
        const TopoDS_Shape cut = applyCutModifier(box, poly, axisFrom, axisTo);
        check(std::abs(shapeVolume(cut) - boxVol) < 1e-3, "collinear polygon: base degismedi");
    }

    // --- 7) Line cut dejenere (cizgi yonu ∥ ref normal) -> base dokunulmaz. ---
    {
        CutModifier line;
        line.kind = CutKind::LineCut;
        line.polygon = {{50, 50, 0}, {50, 50, 100}};  // Z yonunde cizgi
        line.planeNormal = {0, 0, 1};                  // ref normal de Z -> paralel
        line.keepPositive = true;
        const TopoDS_Shape cut = applyCutModifier(box, line, axisFrom, axisTo);
        check(std::abs(shapeVolume(cut) - boxVol) < 1e-3,
              "dejenere line cut (paralel): base degismedi");
    }

    // --- 8) HEB300 I-kesit + polygon cut (nitel: hacim azalir, base sabit). ---
    {
        SteelProfile heb300;
        heb300.name = "HEB300";
        heb300.height = 300.0;
        heb300.width = 300.0;
        heb300.plateThickness = 11.0;
        heb300.flangeThickness = 19.0;
        heb300.roundingRadius = 27.0;
        const TopoDS_Shape beam = extrudeProfileSolid(
            heb300, {0, 0, 0}, {6000, 0, 0});
        const double beamVol = shapeVolume(beam);
        CutModifier poly;
        poly.kind = CutKind::PolygonCut;
        // Govde uzerinde bir dikdortgen cokgen (XY'de), through cut.
        poly.polygon = {{2500, -50, 0}, {3500, -50, 0}, {3500, 50, 0}, {2500, 50, 0}};
        const TopoDS_Shape cut = applyCutModifier(beam, poly, {0, 0, 0}, {6000, 0, 0});
        const double cutVol = shapeVolume(cut);
        check(cutVol > 0.0 && cutVol < beamVol, "HEB300 polygon cut: hacim azaldi");
        check(std::abs(shapeVolume(beam) - beamVol) < 1e-3, "HEB300 taban degismedi");
    }

    // --- 9) Taban immutabilite: tum islemlerden sonra box degismedi. ---
    check(std::abs(shapeVolume(box) - boxVol) < 1e-3, "kutu tabani degismedi");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
