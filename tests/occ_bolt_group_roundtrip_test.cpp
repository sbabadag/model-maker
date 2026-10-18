// BULON GRUBU MMW8 ROUND-TRIP — GERCEK OCCT (Phase B1, Slice 3a kabul).
//
// Iki 10mm levha + M20/22 bulon: bulon kaydi + delikler (owner) + display
// montaj B-Rep'i MMW8 save/load sonrasi da duzenlenebilir kalir. Delikler
// gercek B-Rep silindir kesimi olarak dogrulanir (hacim farki).
//
// Derleme:
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_bolt_group_roundtrip_test.cpp src/document.cpp src/mmw8_codec.cpp \
//       src/occ_solid_store.cpp src/occ_brep_serialize.cpp src/occ_geometry.cpp \
//       src/bolt_geometry.cpp src/geometry.cpp src/cut_modifier.cpp src/bolt.cpp \
//       src/profile_database.cpp src/sha256.cpp src/grid.cpp src/occ_store_null.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_bolt_group_roundtrip_test && ./occ_bolt_group_roundtrip_test

#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol) { return std::abs(a - b) < tol; }

int main() {
    const auto path = std::filesystem::temp_directory_path() / "occ_bolt_roundtrip.mmw8";
    const double plateVol = 100.0 * 100.0 * 10.0;   // 100000
    const double holeVol10 = M_PI * 11.0 * 11.0 * 10.0;  // pi*11^2*10

    // --- Kur: iki levha + bulon ---
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) { std::printf("FAIL no occ store\n"); return 1; }

    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(100.0, 100.0, 10.0).Shape();
    const TopoDS_Shape boxB =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, 0.0, 10.0), 100.0, 100.0, 10.0).Shape();
    doc.addModel(shapeToWireframeWithFaces(boxA, 0.15));
    const EntityId a = doc.modelId(0);
    store->set(a, boxA);
    doc.addModel(shapeToWireframeWithFaces(boxB, 0.15));
    const EntityId b = doc.modelId(1);
    store->set(b, boxB);

    BoltGroup bg;
    bg.parts = {a, b};
    bg.origin = {50.0, 50.0, 10.0};
    bg.axis = {0.0, 0.0, 1.0};
    bg.xDir = {1.0, 0.0, 0.0};
    bg.countX = 2; bg.countY = 1; bg.spacingX = 30.0; bg.spacingY = 0.0;
    bg.params.nominalDiameter = 20.0;
    bg.params.holeDiameter = 22.0;
    bg.params.grade = "8.8";
    const double grip = 20.0;  // iki 10mm levha

    const TopoDS_Shape assembly = buildBoltGroupAssemblyShape(bg, grip);
    check(!assembly.IsNull(), "montaj sekli uretildi (2x1 pattern)");
    doc.pushSnapshot();
    const std::size_t bi = doc.addBoltGroup(bg, shapeToWireframeWithFaces(assembly, 0.15));
    const EntityId boltId = doc.boltGroups()[bi].id;
    const EntityId displayId = doc.boltGroups()[bi].displayEntityId;
    store->set(displayId, assembly);  // display B-Rep

    check(doc.boltGroups().size() == 1, "1 bulon kaydi");
    check(doc.boltGroupHoles(boltId).size() == 4, "4 delik (owner) — 2x1 pattern x 2 parca");

    // Delikleri gercek B-Rep'e uygula: hacim farki.
    {
        double totalCut = 0.0;
        for (const std::size_t ci : doc.boltGroupHoles(boltId)) {
            const auto& m = doc.cutModifiers()[ci];
            const TopoDS_Shape target = m.target == a ? boxA : boxB;
            const TopoDS_Shape cut = applyCutModifier(target, m, {}, {});
            totalCut += (shapeVolume(target) - shapeVolume(cut));
        }
        check(approx(totalCut, 4.0 * holeVol10, 20.0),
              "iki levhada 4 esmerkezli delik (hacim farki 4*pi*11^2*10)");
    }

    // --- Kaydet -> ac ---
    doc.save(path);
    Document loaded;
    loaded.setOccStore(makeOccSolidStore());
    loaded.load(path);
    OccSolidStore* lst = asSolidStore(&loaded.occStore());
    if (!lst) { std::printf("FAIL no occ store after load\n"); return 1; }

    check(loaded.boltGroups().size() == 1, "load: 1 bulon kaydi");
    const EntityId lboltId = loaded.boltGroups()[0].id;
    check(lboltId == boltId, "load: bulon id korundu");
    check(loaded.boltGroups()[0].displayEntityId == displayId, "load: display id korundu");
    check(loaded.boltGroupHoles(lboltId).size() == 4, "load: 4 delik (owner) korundu");
    check(loaded.boltGroups()[0].params.grade == "8.8" &&
              loaded.boltGroups()[0].params.holeDiameter == 22.0,
          "load: parametreler korundu");
    check(loaded.boltGroups()[0].countX == 2 && loaded.boltGroups()[0].countY == 1 &&
              approx(loaded.boltGroups()[0].spacingX, 30.0, 1e-9) &&
              approx(loaded.boltGroups()[0].spacingY, 0.0, 1e-9),
          "load: pattern (countX/countY/spacing) korundu");
    // Display modeli belgede VE B-Rep'i store'da.
    check(loaded.indexOfEntity(displayId).has_value(), "load: display modeli belgede");
    check(lst->find(displayId) != nullptr, "load: display B-Rep store'da");
    // Yukleme sonrasi delikler hala gecerli kesme uretiyor (duzenlenebilir).
    {
        bool holesValid = true;
        for (const std::size_t ci : loaded.boltGroupHoles(lboltId)) {
            const auto& m = loaded.cutModifiers()[ci];
            if (m.kind != CutKind::Hole || m.owner != lboltId || m.diameter != 22.0)
                holesValid = false;
        }
        check(holesValid, "load: delikler Hole/owner/cap tutarli (duzenlenebilir)");
    }
    std::filesystem::remove(path);

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
