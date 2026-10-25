// TEKLA BRIDGE V3 OCC IMPORT — cok parcali + mesh + fitting/boolean + duplicate.
//
// Gercek OCCT kabul testi. Fixture'lar tools/model-maker-bridge/fixtures altinda:
//   * fixture-146-shear-plate-v3.mmbridge  (plate bbox + 6 bolt + weld + V3 dosyalari)
//   * fixture-144-end-plate-v3.mmbridge    (end plate MESH + 2 stiffener bbox +
//                                           2 bolt grubu + 2 weld + fitting op)
//   * fixture-mesh-box-v3.mmbridge         (tek mesh kutu -> mesh->solid)
//
// Derleme: README'deki g++ tek-satiri (src/bridge_package.cpp +
//   bridge_component_mapper.cpp + bridge_occ_adapter.cpp ...).

#include "model_maker/bridge_occ_adapter.hpp"
#include "model_maker/bridge_package.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::fabs(a - b) < tol; }

static BridgePackage load(const char* sub) {
    const std::string dir = std::string(MM_BRIDGE_FIXTURE_DIR) + "/" + sub;
    BridgePackage pkg;
    std::vector<std::string> errors;
    const bool ok = readBridgePackage(dir, pkg, errors);
    check(ok, "paket okundu + dogrulandi");
    if (!ok)
        for (const auto& e : errors) std::printf("  err: %s\n", e.c_str());
    return pkg;
}

static bool hasParam(const ComponentInstanceRecord& rec, const std::wstring& name) {
    for (const auto& p : rec.params)
        if (p.name == name) return true;
    return false;
}

int main() {
    // ================= PART A: mesh -> solid (birim) =========================
    {
        BridgeMesh empty;
        check(buildBridgeMeshSolid(empty).IsNull(), "mesh->solid: bos mesh -> null");

        const BridgePackage pkg = load("fixture-mesh-box-v3.mmbridge");
        if (!pkg.objects.empty() && pkg.objects[0].mesh) {
            const TopoDS_Shape s = buildBridgeMeshSolid(*pkg.objects[0].mesh);
            check(!s.IsNull(), "mesh->solid: kutu uretildi");
            if (!s.IsNull())
                check(approx(shapeVolume(s), 340000.0, 1.0),
                      "mesh->solid: hacim 340000 mm^3");
        }
    }

    // ================= PART B: transform (birim) =============================
    {
        BridgeBBox bb;
        bb.min = {0, 0, 0};
        bb.max = {10, 10, 10};
        const TopoDS_Shape box = buildBridgePlateSolid(bb);
        BridgeTransform tr;
        tr.origin = {100, 0, 0};
        tr.xAxis = {1, 0, 0};
        tr.yAxis = {0, 1, 0};
        tr.zAxis = {0, 0, 1};
        tr.valid = true;
        const TopoDS_Shape moved = transformBridgeShape(box, tr);
        check(!moved.IsNull(), "transform: kutu tasindi");
        if (!moved.IsNull())
            check(approx(shapeVolume(moved), 1000.0, 0.1), "transform: hacim korundu");
    }

    // ================= PART C: 146 V3 (delme yolu) ===========================
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        const BridgePackage pkg = load("fixture-146-shear-plate-v3.mmbridge");
        const BridgeImportOutcome out = importBridgeComponent(doc, pkg);
        check(out.ok, "146 V3 import ok");
        if (out.ok) {
            check(out.partsImported == 1, "146 V3: 1 parca");
            check(out.boltGroupsImported == 1, "146 V3: 1 bulon grubu");
            check(out.holesCut == 0, "146 V3: unverified axis/dia -> 0 otomatik delik");
            check(out.plateId != kInvalidEntityId && out.boltGroupId != kInvalidEntityId,
                  "146 V3: plate + bolt id");
            OccSolidStore* store = asSolidStore(&doc.occStore());
            const TopoDS_Shape* ps = store ? store->find(out.plateId) : nullptr;
            if (ps) check(approx(shapeVolume(*ps), 340000.0, 2.0), "146 V3: deliksiz hacim");
            check(doc.componentInstances().size() == 1, "146 V3: 1 instance");
            if (!doc.componentInstances().empty()) {
                const auto& rec = doc.componentInstances()[0];
                check(rec.schemaVersion == 3, "146 V3: schemaVersion 3");
                check(hasParam(rec, L"_bridge_weld"), "146 V3: weld metadata");
                // Native parametrik esleme sonucu metadata olarak kayitli.
                check(hasParam(rec, L"_bridge_native_typeId"), "146 V3: native typeId metadata");
                check(hasParam(rec, L"_bridge_native_state"), "146 V3: native state metadata");
                const std::wstring* st = nullptr;
                for (const auto& p : rec.params)
                    if (p.name == L"_bridge_native_state" &&
                        std::holds_alternative<std::wstring>(p.value))
                        st = &std::get<std::wstring>(p.value);
                check(st && *st == L"unverified",
                      "146 V3: native state = unverified (primary eksen yok, generator uydurmadi)");
            }
            // MMW8 save/load (component instance kalici).
            // (Round-trip OCC store testi ayri; burada yalniz instance varligi.)
        }
    }

    // ================= PART D: 144 V3 (cok parcali + mesh) ===================
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        const BridgePackage pkg = load("fixture-144-end-plate-v3.mmbridge");
        const BridgeImportOutcome out = importBridgeComponent(doc, pkg);
        check(out.ok, "144 V3 import ok");
        if (out.ok) {
            check(out.partsImported == 3, "144 V3: 3 parca (end plate + 2 stiffener)");
            check(out.boltGroupsImported == 2, "144 V3: 2 bulon grubu (display)");
            check(out.holesCut == 0, "144 V3: 0 delik (mesh hedef zaten delikli)");
            check(out.partIds.size() == 3 && out.boltGroupIds.size() == 2,
                  "144 V3: 3 partId + 2 boltGroupId");

            // end plate mesh -> solid (buyuk kutu 120x600x400).
            OccSolidStore* store = asSolidStore(&doc.occStore());
            const TopoDS_Shape* ep = store ? store->find(out.partIds[0]) : nullptr;
            if (ep) check(approx(shapeVolume(*ep), 28800000.0, 10.0),
                          "144 V3: end plate mesh hacim ~28.8M");

            check(doc.componentInstances().size() == 1, "144 V3: 1 instance");
            if (!doc.componentInstances().empty()) {
                const auto& rec = doc.componentInstances()[0];
                check(rec.ownedPlateIds.size() == 3, "144 V3: 3 owned plate");
                check(rec.ownedBoltGroupIds.size() == 2, "144 V3: 2 owned bolt");
                check(hasParam(rec, L"_bridge_fitting_beam_fitting"), "144 V3: fitting metadata (objects)");
                check(hasParam(rec, L"_bridge_boolean_beam_cut"), "144 V3: boolean metadata (objects)");
                check(hasParam(rec, L"_bridge_op_0"), "144 V3: fitting op metadata (operations)");
            }
            // weld sayisi 2 -> _bridge_weld ve _bridge_weld_1.
            if (!doc.componentInstances().empty()) {
                const auto& rec = doc.componentInstances()[0];
                int weldParams = 0;
                for (const auto& p : rec.params)
                    if (p.name == L"_bridge_weld" || p.name == L"_bridge_weld_1") ++weldParams;
                check(weldParams == 2, "144 V3: 2 weld metadata");
            }
        }
    }

    // ================= PART E: duplicate import ==============================
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        const BridgePackage pkg = load("fixture-146-shear-plate-v3.mmbridge");
        const BridgeImportOutcome first = importBridgeComponent(doc, pkg);
        check(first.ok && !first.duplicate, "dup: ilk import ok");
        const std::size_t before = doc.componentInstances().size();
        const BridgeImportOutcome second = importBridgeComponent(doc, pkg);
        check(second.ok && second.duplicate, "dup: ikinci import duplicate");
        check(doc.componentInstances().size() == before, "dup: yeni instance YOK");
        check(second.componentId == first.componentId, "dup: ayni componentId");
    }

    // ================= PART F: eksik hedef / kotu paket ======================
    {
        // objects.json bos paket -> import edilebilir parca yok -> hata.
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        BridgePackage pkg = load("fixture-mesh-box-v3.mmbridge");
        pkg.objects.clear();
        const BridgeImportOutcome out = importBridgeComponent(doc, pkg);
        check(!out.ok, "bos objects -> import basarisiz");
        check(doc.models().empty(), "bos objects -> belge degismedi");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
