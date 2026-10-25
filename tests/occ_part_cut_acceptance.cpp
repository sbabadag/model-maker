// P3 KABUL SENARYOSU — GERCEK OCCT dogrulama (Part Cut = BooleanCut).
//
// Tekla semantigi: "cut a part using another part" — hedef part (kimligi
// korunur) icinden kesici part cikarilir (Boolean difference). Kesici parca
// ayri bir varliktir; tasindiginda hedef yeniden hesaplanir. Part cut baska
// nesneleri ETKILEMEZ.
//
// 100x100x100 hedef kutu + 40x40x100 kesici kutu ([30,70]x[30,70]) ile KESIN
// hacim oranlari:
//   1) BooleanCut (resolver ile) -> 40x40 sutun cikarilir (~0.84)
//   2) kesici parca DEGISMEDI (immutabilite)
//   3) hedef taban DEGISMEDI
//   4) resolver YOKKEN BooleanCut -> base DOKUNULMAZ
//   5) zincir: partCut + planeCut(x=80, keep x<80) -> ~0.64
//
// Derleme (bu host, Linux OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_part_cut_acceptance.cpp src/occ_geometry.cpp src/geometry.cpp \
//       src/cut_modifier.cpp src/profile_database.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_part_cut_acceptance && ./occ_part_cut_acceptance

#include "model_maker/cut_modifier.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

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

int main() {
    const TopoDS_Shape target = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Shape();
    const TopoDS_Shape tool =
        BRepPrimAPI_MakeBox(gp_Pnt(30.0, 30.0, 0.0), 40.0, 40.0, 100.0).Shape();
    const double targetVol = shapeVolume(target);
    const double toolVol = shapeVolume(tool);
    check(std::abs(targetVol - 1.0e6) < 1.0, "hedef kutu ~1e6 mm^3");
    check(std::abs(toolVol - 160000.0) < 1.0, "kesici kutu ~160000 mm^3");

    const Vec3 axisFrom{0.0, 0.0, 0.0};
    const Vec3 axisTo{100.0, 0.0, 0.0};

    const EntityId toolId = 1;
    const EntityId targetId = 2;
    const ToolShapeResolver resolve = [&](EntityId id) -> const TopoDS_Shape* {
        return (id == toolId) ? &tool : nullptr;
    };

    CutModifier partCut;
    partCut.kind = CutKind::BooleanCut;
    partCut.target = targetId;
    partCut.tool = toolId;
    partCut.enabled = true;

    // --- 1) BooleanCut (resolver ile): 40x40x100 sutun cikarilir -> 0.84. ---
    {
        const TopoDS_Shape cut = applyCutModifier(target, partCut, axisFrom, axisTo, resolve);
        const double ratio = shapeVolume(cut) / targetVol;
        check(std::abs(ratio - 0.84) < 0.01, "part cut: ~0.84 kati kalir");
    }

    // --- 2) Kesici parca degismedi. ---
    check(std::abs(shapeVolume(tool) - toolVol) < 1e-3, "kesici parca degismedi");

    // --- 3) Hedef taban degismedi. ---
    check(std::abs(shapeVolume(target) - targetVol) < 1e-3, "hedef taban degismedi");

    // --- 4) Resolver YOKKEN -> base DOKUNULMAZ. ---
    {
        const TopoDS_Shape cut = applyCutModifier(target, partCut, axisFrom, axisTo);
        check(std::abs(shapeVolume(cut) - targetVol) < 1e-3,
              "resolver yokken part cut: base degismedi");
    }

    // --- 5) Zincir: partCut + planeCut(x=80, keep x<80) -> ~0.64. ---
    {
        CutModifier plane;
        plane.kind = CutKind::PlaneCut;
        plane.planeOrigin = {80.0, 0.0, 0.0};
        plane.planeNormal = {1.0, 0.0, 0.0};
        plane.keepPositive = false;  // x<80 korunur
        std::vector<CutModifier> mods{partCut, plane};
        const TopoDS_Shape cut =
            applyCutModifierChain(target, mods, axisFrom, axisTo, resolve);
        // x<80 (800000) - delik 160000 = 640000 -> 0.64.
        const double ratio = shapeVolume(cut) / targetVol;
        check(std::abs(ratio - 0.64) < 0.01, "zincir partCut+planeCut: ~0.64 kati kalir");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
