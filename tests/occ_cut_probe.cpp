// KESME MODIFIER uygulamasi (Phase 4.2) — GERCEK OCCT dogrulama.
//
// cutSolidByPlane / applyCutModifier / applyCutModifierChain'in gercek B-Rep
// uzerindeki davranisini olcer: Fitting "kisa taraf otomatik atilir" semantigi,
// PlaneCut keepPositive, coklu modifier sirali uygulama ve base'in degismezligi.
//
// Derleme (bu host, Linux OCCT 7.9):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_cut_probe.cpp src/occ_geometry.cpp src/geometry.cpp \
//       src/cut_modifier.cpp src/profile_database.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_cut_probe && ./occ_cut_probe

#include "model_maker/cut_modifier.hpp"
#include "model_maker/occ_geometry.hpp"

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

static bool nearD(double a, double b, double tol) { return std::abs(a - b) < tol; }

static void bbox(const TopoDS_Shape& s, double& xMin, double& yMin, double& zMin,
                 double& xMax, double& yMax, double& zMax) {
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    b.Get(xMin, yMin, zMin, xMax, yMax, zMax);
}

int main() {
    // "Kiris": X boyunca 100x50x20 kutu; profil ekseni merkezden.
    TopoDS_Shape base = BRepPrimAPI_MakeBox(100.0, 50.0, 20.0).Shape();
    const Vec3 axisFrom{0, 25, 10};
    const Vec3 axisTo{100, 25, 10};
    const double baseVol = shapeVolume(base);
    check(nearD(baseVol, 100000.0, 1.0), "taban kutu hacmi 100000");

    // --- Fitting: duzlem x=20 (from'a yakin) -> kisa taraf (0..20) atilir. ---
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {20, 0, 0};
        fit.planeNormal = {1, 0, 0};
        TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(nearD(shapeVolume(cut), 80000.0, 1.0), "fitting: kisa from-tarafi atildi (hacim 80000)");
        check(nearD(xMin, 20.0, 1e-6) && nearD(xMax, 100.0, 1e-6),
              "fitting: bbox x in [20,100]");
    }

    // --- Fitting: duzlem x=80 (to'ya yakin) -> kisa taraf (80..100) atilir. ---
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {80, 0, 0};
        fit.planeNormal = {1, 0, 0};
        TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(nearD(shapeVolume(cut), 80000.0, 1.0), "fitting: kisa to-tarafi atildi (hacim 80000)");
        check(nearD(xMin, 0.0, 1e-6) && nearD(xMax, 80.0, 1e-6),
              "fitting: bbox x in [0,80]");
    }

    // --- Fitting: kesmeyen duzlem (uzatma) -> base aynen (prototip siniri). ---
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {150, 0, 0};
        fit.planeNormal = {1, 0, 0};
        TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        check(nearD(shapeVolume(cut), baseVol, 1.0), "fitting: kesmeyen duzlem base'i korur");
    }

    // --- PlaneCut: duzlem x=30, keepPositive=false -> x<30 korunur. ---
    {
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.planeOrigin = {30, 0, 0};
        pc.planeNormal = {1, 0, 0};
        pc.keepPositive = false;
        TopoDS_Shape cut = applyCutModifier(base, pc, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(nearD(shapeVolume(cut), 30000.0, 1.0), "planeCut: -X tarafi korunur (hacim 30000)");
        check(nearD(xMin, 0.0, 1e-6) && nearD(xMax, 30.0, 1e-6),
              "planeCut: bbox x in [0,30]");
    }

    // --- Zincir: Fitting(x=20, koru +X) sonra PlaneCut(x=80, koru +X) ---
    // -> x in [80,100], hacim 20*50*20 = 20000.
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {20, 0, 0};
        fit.planeNormal = {1, 0, 0};
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.planeOrigin = {80, 0, 0};
        pc.planeNormal = {1, 0, 0};
        pc.keepPositive = true;
        std::vector<CutModifier> mods{fit, pc};
        TopoDS_Shape cut = applyCutModifierChain(base, mods, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(nearD(shapeVolume(cut), 20000.0, 1.0), "zincir: iki modifier sirali (hacim 20000)");
        check(nearD(xMin, 80.0, 1e-6) && nearD(xMax, 100.0, 1e-6),
              "zincir: bbox x in [80,100]");
    }

    // --- Devre disi modifier zincirde atlanir. ---
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {20, 0, 0};
        fit.planeNormal = {1, 0, 0};
        fit.enabled = false;
        std::vector<CutModifier> mods{fit};
        TopoDS_Shape cut = applyCutModifierChain(base, mods, axisFrom, axisTo);
        check(nearD(shapeVolume(cut), baseVol, 1.0), "zincir: etkin-degil modifier atlanir");
    }

    // --- Immutabilite: base'in hacmi/bbox'i bu islemlerden sonra DEGISMEDI. ---
    check(nearD(shapeVolume(base), 100000.0, 1.0), "base kati degismedi (orijinal B-Rep korunur)");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
