// TEKLA 146 (Shear plate simple) BRIDGE OCC IMPORT — gercek OCCT kabul testi.
//
// Dogrulanmis Tekla 2025 SP6 degerleriyle (fixture-146-shear-plate-v2):
//   * plaka bbox (4.25,195,6959)..(14.25,365,7159) -> 10x170x200, hacim 340000.
//   * 6 x M20 bulon, eksen +X (inferred), delik capi 22 (standard), X=0 konum
//     levha disinda GORUNUR ama eksen X boyunca projeksiyon levhayi deler.
//   * 6 delik kesilir; hacim ~317192 (340000 - 6*pi*11^2*10).
//   * Document'a atomik import + undo/redo + negatif (dis bulon delinmez).
//
// Derleme (Linux + OCCT): testin baslik yorumundaki kaynak listesi +
//   src/bridge_package.cpp src/bridge_component_mapper.cpp src/bridge_occ_adapter.cpp
//   -DMM_BRIDGE_FIXTURE_DIR="$(pwd)/tools/model-maker-bridge/fixtures"

#include "model_maker/bridge_occ_adapter.hpp"
#include "model_maker/bridge_package.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

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

static void bboxOf(const TopoDS_Shape& s, double& mnx, double& mny, double& mnz,
                   double& mxx, double& mxy, double& mxz) {
    Bnd_Box bb;
    BRepBndLib::Add(s, bb);
    bb.Get(mnx, mny, mnz, mxx, mxy, mxz);
}

int main() {
    const std::string dir =
        std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-146-shear-plate-v2.mmbridge";
    BridgePackage pkg;
    std::vector<std::string> errors;
    const bool ok = readBridgePackage(dir, pkg, errors);
    check(ok, "V2 paket okundu + dogrulandi");
    if (!ok)
        for (const auto& e : errors) std::printf("  err: %s\n", e.c_str());

    check(pkg.manifest.formatVersion == "2.0", "formatVersion 2.0");
    check(pkg.component.number == 146, "component 146");
    check(pkg.component.primaryMember.role == "COLUMN" &&
              pkg.component.primaryMember.profile == "HEA400",
          "primary = HEA400 COLUMN (5165)");
    check(pkg.component.secondaryMembers.size() == 1 &&
              pkg.component.secondaryMembers[0].role == "BEAM" &&
              pkg.component.secondaryMembers[0].profile == "HEA300",
          "secondary = HEA300 BEAM (5213)");

    const BridgeObject* plate = nullptr;
    const BridgeObject* bolts = nullptr;
    const BridgeObject* weld = nullptr;
    for (const auto& o : pkg.objects) {
        if (o.kind == "ContourPlate") plate = &o;
        if (o.kind == "BoltGroup") bolts = &o;
        if (o.kind == "Weld") weld = &o;
    }
    check(plate && plate->bbox.has_value(), "plaka + bbox");
    check(plate->topology.has_value() && plate->topology->faceCount == 6 &&
              plate->topology->holeLoops == 0,
          "plaka topolojisi: 6 yuz, 0 delik loop");
    check(bolts && bolts->boltGroup.has_value() && bolts->boltGroup->bolts.size() == 6,
          "bulon grubu 6 bulon");
    check(weld && weld->weld.has_value() && weld->weld->size == 5.0, "weld 5 mm fillet");

    // ================= PART A: plaka kati (PoC) ==============================
    {
        const TopoDS_Shape solid = buildBridgePlateSolid(*plate->bbox);
        check(!solid.IsNull(), "plaka kati uretildi");
        double mnx, mny, mnz, mxx, mxy, mxz;
        bboxOf(solid, mnx, mny, mnz, mxx, mxy, mxz);
        check(approx(mnx, 4.25) && approx(mxx, 14.25), "plaka X 4.25..14.25");
        check(approx(mny, 195.0) && approx(mxy, 365.0), "plaka Y 195..365");
        check(approx(mnz, 6959.0) && approx(mxz, 7159.0), "plaka Z 6959..7159");
        check(approx(mxx - mnx, 10.0) && approx(mxy - mny, 170.0) && approx(mxz - mnz, 200.0),
              "plaka boyutlari 10x170x200");
        check(approx(shapeVolume(solid), 340000.0, 0.5), "plaka hacmi 340000 mm^3");
    }

    // ================= PART B: bulon plani ====================================
    {
        const BridgeBoltPlan plan = planBridgeBolts(*bolts->boltGroup, *plate->bbox);
        check(plan.ok, "planBridgeBolts ok");
        check(plan.axis.x == 1.0 && plan.axis.y == 0.0 && plan.axis.z == 0.0, "eksen +X");
        check(plan.axisSource == "inferred", "eksen kaynagi: inferred");
        check(approx(plan.holeDiameter, 22.0), "delik capi 22 mm (M20 + 2 tol)");
        check(plan.diameterSource == "standard", "cap kaynagi: standard (ISO)");
        check(plan.holeOrigins.size() == 6, "6 delik orijini");
        int inside = 0;
        for (bool b : plan.boltInside)
            if (b) ++inside;
        check(inside == 6, "6 bulon plaka ayak izinde (X=0 eksen=X projeksiyonuyla DISLANMADI)");
    }

    // ================= PART C: delik kesimi ===================================
    {
        const TopoDS_Shape solid = buildBridgePlateSolid(*plate->bbox);
        const BridgeBoltPlan plan = planBridgeBolts(*bolts->boltGroup, *plate->bbox);
        const BridgeHoleCutResult cut = cutBridgeBoltHoles(solid, plan);
        check(cut.ok, "cutBridgeBoltHoles ok");
        check(cut.holesCut == 6 && cut.holesSkipped == 0, "6 delik kesildi / 0 atlandi");
        check(approx(shapeVolume(cut.solid), 317192.0, 2.0), "delikli hacim ~317192 mm^3");
    }

    // ================= PART D: Document'a atomik import ========================
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        const BridgeImportOutcome out = importBridgeComponent(doc, pkg);
        check(out.ok, "importBridgeComponent ok");
        if (out.ok) {
            check(out.plateId != kInvalidEntityId, "plaka EntityId");
            check(out.boltGroupId != kInvalidEntityId, "bulon EntityId");
            check(out.componentId != kInvalidEntityId, "component EntityId");
            check(out.holesCut == 6, "6 delik kayitli");

            OccSolidStore* store = asSolidStore(&doc.occStore());
            const TopoDS_Shape* ps = store ? store->find(out.plateId) : nullptr;
            check(ps && !ps->IsNull(), "Document plaka kati mevcut");
            if (ps) check(approx(shapeVolume(*ps), 317192.0, 2.0), "Document plaka kati delikli");

            check(doc.componentInstances().size() == 1, "1 component instance");
            const auto& rec = doc.componentInstances()[0];
            check(rec.typeId == L"tekla_bridge_import", "typeId tekla_bridge_import (orphan)");
            check(rec.ownedPlateIds.size() == 1 && rec.ownedBoltGroupIds.size() == 1,
                  "owned plate + bolt");
            check(rec.children.size() == 2, "2 child (shear_tab_near + beam_bolts)");

            bool hasPrimary = false, hasWeld = false;
            for (const auto& p : rec.params) {
                if (p.name == L"_bridge_primary" &&
                    std::holds_alternative<std::wstring>(p.value) &&
                    std::get<std::wstring>(p.value).find(L"5165") != std::wstring::npos)
                    hasPrimary = true;
                if (p.name == L"_bridge_weld") hasWeld = true;
            }
            check(hasPrimary, "primary ref (5165) metadata kayitli");
            check(hasWeld, "weld metadata kayitli");

            // Undo/redo tek adimda.
            check(doc.undo(), "undo import");
            check(doc.componentInstances().empty(), "undo: instance silindi");
            check(doc.redo(), "redo import");
            check(doc.componentInstances().size() == 1, "redo: instance geri geldi");
        }
    }

    // ================= PART E: negatif — dis bulon delinmez ===================
    {
        BridgeBoltGroupInfo info;
        info.size = "M20";
        info.axis = "x";
        BridgeBolt b1;
        b1.position = {0, 265, 7119};
        b1.nominalDiameter = 20;  // iceride
        BridgeBolt b2;
        b2.position = {0, 999, 7119};
        b2.nominalDiameter = 20;  // Y=999 DISARIDA
        info.bolts = {b1, b2};

        BridgeBBox bb;
        bb.min = {4.25, 195, 6959};
        bb.max = {14.25, 365, 7159};

        const BridgeBoltPlan plan = planBridgeBolts(info, bb);
        check(plan.ok, "negatif: plan ok");
        check(plan.boltInside.size() == 2 && plan.boltInside[0] && !plan.boltInside[1],
              "negatif: dis bulon isaretlendi");
        check(!plan.diagnostics[1].empty(), "negatif: dis bulon icin diagnostic var");

        const TopoDS_Shape solid = buildBridgePlateSolid(bb);
        const BridgeHoleCutResult cut = cutBridgeBoltHoles(solid, plan);
        check(cut.holesCut == 1 && cut.holesSkipped == 1,
              "negatif: 1 delik kesildi / 1 atlandi (dis bulon delinmedi)");
    }

    // ================= PART F: atomik rollback (hata -> belge degismez) ======
    {
        // OCC magaza ENJEKTE EDILMEZ -> import, plaka ekledikten SONRA basarisiz
        // olmali ve rollbackOpenRecord ile belgeyi TAMAMEN geri almali.
        Document doc;  // no occ store (no-op store)
        const BridgeImportOutcome out = importBridgeComponent(doc, pkg);
        check(!out.ok, "OCC magaza yok -> import basarisiz");
        check(doc.models().empty(), "rollback: plaka modeli geri alindi");
        check(doc.componentInstances().empty(), "rollback: component instance yok");
        check(!doc.hasOpenUndoRecord(), "rollback: acik undo kaydi kalmadi");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
