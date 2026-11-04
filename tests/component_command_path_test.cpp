// KATALOG KOMUT YOLU (MM-REGRESYON) — gercek OCCT kabul testi.
//
// Regresyon (Asus, 425a9c4): End Plate PAKETI (katalog komutu mm_end_plate_pkg)
// standart dugumde HICBIR SEY yapmiyordu — ne diyalog ne uyari ne log. Yerlesik
// End Plate calisiyordu; sorun yalniz katalog/paket yolundaydi. Bisect: MM-7
// check (19352a1) recipe'ye fail-closed baglandi; dar flansli IPE kiriste
// "flans/2" gauge 50mm < EC3 p2 52.8mm -> plan.ok=false ve komut sessizce dustu.
//
// Bu test GERCEK komut yolunu kurar ve su seyleri dogrular:
//   1. UYUMLU profil (200x400) -> plan.ok + instance OLUSUR (komut yolu calisir).
//   2. DAR flans (IPE200 100x200) -> varsayilan gauge OTOMATIK buyur (MM-7 item 1:
//      max(flans/2, 2.4*d0, tw+2*kaynak+d0) -> 55 mm); plan GECERLI kurulur.
//   3. EGIK kiris (30°) -> plan.ok=false + "egik kiris" ACIK ret.
//   4. ACIK kucuk boltSpacingX (40) -> kontrol yine fail-closed reddeder (pitch_p2).
//
// Document -> componentInputFromDocument (GUI ile AYNI kanonik yol) ->
// runRecipe(mm_end_plate_pkg) -> ComponentMaterializer (prepare+commit).

#include "model_maker/component_engine.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/document.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/recipe.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

static std::vector<SteelProfile> catalog() {
    std::vector<SteelProfile> cat;
    cat.push_back(makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0));
    cat.push_back(makeProfile("IPE400", 400.0, 200.0, 8.6, 13.5));  // uyumlu (MM-7 gecer)
    cat.push_back(makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5));   // dar flans (MM-7 reddeder)
    return cat;
}

// Dokumana kolon + kiris ekler: gercek I-kesit kati + wireframe modeli + ozellikler.
// Donus: (colId, beamId).
static std::pair<EntityId, EntityId> addNode(Document& doc, OccSolidStore* store,
                                             const SteelProfile& colP, const SteelProfile& beamP,
                                             double bx, double by, double bz) {
    // Kolon: dusey HEB300 (0,0,0)->(0,0,3000).
    const TopoDS_Shape colSolid = extrudeProfileSolid(colP, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
    WireframeModel colModel = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colModel.properties();
        p.profileName = colP.name;
        p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 0;
        p.axisToX = 0; p.axisToY = 0; p.axisToZ = 3000;
        p.sectionNormalX = 0; p.sectionNormalY = 0; p.sectionNormalZ = 1;
        p.profileRotation = 0;
        p.sectionAnchor = SectionAnchor::Center;
        colModel.setProperties(std::move(p));
    }
    doc.addModel(std::move(colModel));
    const EntityId colId = doc.modelId(doc.models().size() - 1);
    if (store && !colSolid.IsNull()) {
        store->set(colId, colSolid);
        store->setBaseVersion(colId, store->activeVersion(colId));
    }

    // Kiris: yatay, (0,0,3000) -> (bx,by,bz).
    const TopoDS_Shape beamSolid = extrudeProfileSolid(beamP, {0, 0, 3000}, {bx, by, bz}, 0.0, {0, 0, 1});
    WireframeModel beamModel = shapeToWireframeWithFaces(beamSolid, 0.15);
    {
        EntityProperties p = beamModel.properties();
        p.profileName = beamP.name;
        p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 3000;
        p.axisToX = bx; p.axisToY = by; p.axisToZ = bz;
        p.sectionNormalX = 0; p.sectionNormalY = 0; p.sectionNormalZ = 1;
        p.profileRotation = 0;
        p.sectionAnchor = SectionAnchor::Center;
        beamModel.setProperties(std::move(p));
    }
    doc.addModel(std::move(beamModel));
    const EntityId beamId = doc.modelId(doc.models().size() - 1);
    if (store && !beamSolid.IsNull()) {
        store->set(beamId, beamSolid);
        store->setBaseVersion(beamId, store->activeVersion(beamId));
    }
    return {colId, beamId};
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto cat = catalog();

    // Paketi yukle (katalog komutunun tipi).
    ComponentRegistry pkgReg;
    registerBuiltInComponents(pkgReg);
    const auto pkg = loadComponentPackage(
        std::filesystem::path(MM_PACKAGES_DIR) / "end_plate_pkg", pkgReg);
    check(pkg.ok, "end_plate_pkg paketi yuklendi");
    if (!pkg.ok) { std::printf("  paket hatasi: %ls\n", pkg.message.c_str()); return 1; }

    // --- 1. UYUMLU PROFIL (200x400): komut yolu calisir, instance olusur -----
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        const auto [colId, beamId] = addNode(doc, store,
            makeProfile("HEB300", 300, 300, 11, 19),
            makeProfile("IPE400", 400, 200, 8.6, 13.5), 2000.0, 0.0, 3000.0);

        auto in = componentInputFromDocument(doc, cat, colId, {beamId});
        check(in.has_value(), "uyumlu: modelden girdi kuruldu (kanonik yol)");
        if (!in) return 1;

        const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
        check(plan.ok, "uyumlu: runRecipe plan ok");
        if (!plan.ok) { std::printf("  error=%ls\n", plan.error.c_str()); return 1; }

        ComponentMaterializer mat(doc);
        const ComponentPlan cplan = componentPlanFromGenerationPlan(plan);
        ComponentMaterializer::Prepared prep = mat.prepare(cplan, colId, {beamId});
        check(prep.ok, "uyumlu: prepare ok");
        if (!prep.ok) { std::printf("  error=%ls\n", prep.error.c_str()); return 1; }

        ComponentInstanceRecord rec;
        rec.typeId = pkg.typeId;
        rec.schemaVersion = 1;
        rec.mainPart = colId;
        rec.secondaryParts = {beamId};
        const ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "uyumlu: commit ok (instance olustu)");
        check(doc.componentInstances().size() == 1, "uyumlu: 1 component instance kaydi");
        check(res.roleToEntity.count("end_plate") == 1, "uyumlu: end_plate -> EntityId");
        check(res.roleToEntity.count("beam_bolts") == 1, "uyumlu: beam_bolts -> EntityId");
        check(!res.ownedPlateIds.empty(), "uyumlu: owned levha var");
        check(!res.ownedBoltGroupIds.empty(), "uyumlu: owned bulon grubu var");

        // Levha kati gercek OCCT (deliklerle kesilmis, hacim > 0).
        const EntityId plateId = res.roleToEntity.at("end_plate");
        if (store) {
            const TopoDS_Shape* ps = store->find(plateId);
            check(ps && !ps->IsNull() && shapeVolume(*ps) > 0.0,
                  "uyumlu: end plate OCCT kati uretildi");
        }
    }

    // --- 2. DAR FLANS (IPE200 100x200): varsayilan gauge OTOMATIK buyur ------
    //        (MM-7 item 1: max(flans/2, 2.4*d0, tw+2*kaynak+d0) -> 55 mm);
    //        plan GECERLI kurulur (eskiden fail-closed'du).
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        const auto [colId, beamId] = addNode(doc, store,
            makeProfile("HEB300", 300, 300, 11, 19),
            makeProfile("IPE200", 200, 100, 5.6, 8.5), 2000.0, 0.0, 3000.0);

        auto in = componentInputFromDocument(doc, cat, colId, {beamId});
        check(in.has_value(), "dar: modelden girdi kuruldu");
        if (in) {
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
            check(plan.ok, "dar: varsayilan gauge otomatik -> plan GECERLI");
            if (plan.ok && plan.boltGroups.size() == 1) {
                check(std::fabs(plan.boltGroups[0].spacingX - 55.0) < 1e-9,
                      "dar: spacingX == 55 (2.4*d0 karsilanir)");
                check(plan.boltGroups[0].spacingX >= 2.4 * 22.0,
                      "dar: spacingX >= 2.4*d0");
            }
        }
    }

    // --- 4. ACIK KUCUK boltSpacingX (IPE200): kontrol fail-closed reddeder ----
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        const auto [colId, beamId] = addNode(doc, store,
            makeProfile("HEB300", 300, 300, 11, 19),
            makeProfile("IPE200", 200, 100, 5.6, 8.5), 2000.0, 0.0, 3000.0);

        auto in = componentInputFromDocument(doc, cat, colId, {beamId});
        check(in.has_value(), "acik: modelden girdi kuruldu");
        if (in) {
            in->params.push_back(CompParam{L"boltSpacingX", CompParamValue{40.0}});
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
            check(!plan.ok, "acik: ACIK kucuk boltSpacingX -> fail-closed");
            check(!plan.error.empty(), "acik: hata mesaji dolu (ACIK)");
            bool p2Fail = false;
            for (const auto& c : plan.checks)
                if (c.id == "pitch_p2" && !c.pass) p2Fail = true;
            check(p2Fail, "acik: pitch_p2 ihlali kaydi dolu");
        }
    }

    // --- 3. EGIK KIRIS (30°): ACIK "egik kiris" ret --------------------------
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        const auto [colId, beamId] = addNode(doc, store,
            makeProfile("HEB300", 300, 300, 11, 19),
            makeProfile("IPE400", 400, 200, 8.6, 13.5), 1732.0, 1000.0, 3000.0);

        auto in = componentInputFromDocument(doc, cat, colId, {beamId});
        check(in.has_value(), "egik: modelden girdi kuruldu");
        if (in) {
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
            check(!plan.ok, "egik: plan reddedildi");
            check(!plan.error.empty(), "egik: hata mesaji dolu");
            if (!plan.error.empty())
                std::printf("  egik kiris hatasi: %ls\n", plan.error.c_str());
            check(plan.error.find(L"egik") != std::wstring::npos ||
                  plan.error.find(L"DIK") != std::wstring::npos,
                  "egik: mesaj desteklenmeyen egim oldugunu soyler");
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
