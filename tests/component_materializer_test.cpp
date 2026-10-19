// COMPONENT MATERIALIZER (C2) — gercek OCCT kabul testi.
//
// ComponentBuilder -> ComponentPlan -> ComponentMaterializer: levha + bulon
// grubu GERCEK Document + OccSolidStore nesnesine donusur; roleKey -> EntityId
// eslemesi kurulur; ownership ComponentInstanceRecord'a yazilir; Weld acik
// Unsupported reddedilir; birden fazla component bagimsiz sahiplenir.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/component_materializer_test.cpp \
//       src/component_materializer.cpp src/component_framework.cpp \
//       src/component_engine.cpp src/document.cpp src/mmw8_codec.cpp \
//       src/end_plate.cpp src/bolt.cpp src/cut_modifier.cpp src/geometry.cpp \
//       src/profile_database.cpp src/reference_geometry.cpp \
//       src/constraint_system.cpp src/contact_resolver.cpp \
//       src/geometry_reference_resolver.cpp src/magnetic_frame.cpp \
//       src/component_recompute.cpp src/component.cpp src/assembly.cpp \
//       src/occ_contact_resolver.cpp src/occ_solid_store.cpp \
//       src/occ_brep_serialize.cpp src/occ_geometry.cpp src/bolt_geometry.cpp \
//       src/end_plate_geometry.cpp src/sha256.cpp src/grid.cpp \
//       src/occ_store_null.cpp src/occ_bridge.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -o component_materializer_test && ./component_materializer_test

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

int main() {
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    ComponentMaterializer mat(doc);

    // --- Fin Plate benzeri: levha + bulon grubu + sahiplik -------------------
    PlateSpec plate;
    plate.center = {0.0, 0.0, 0.0};
    plate.u = {1.0, 0.0, 0.0};
    plate.v = {0.0, 1.0, 0.0};
    plate.axis = {0.0, 0.0, 1.0};
    plate.width = 120.0;
    plate.height = 240.0;
    plate.thickness = 10.0;
    plate.name = L"Fin Levhasi";

    BoltGroup bolts;
    bolts.origin = {0.0, -80.0, 0.0};
    bolts.axis = {0.0, 0.0, 1.0};
    bolts.xDir = {1.0, 0.0, 0.0};
    bolts.countX = 1;
    bolts.countY = 2;
    bolts.spacingY = 160.0;
    bolts.params.nominalDiameter = 20.0;
    bolts.params.holeDiameter = 22.0;

    // main = kullanici parcası (kolon), secondary = kiris. Bu testte basit bir
    // main model ekleyip EntityId'sini kullaniriz.
    WireframeModel col = WireframeModel::cube(100.0);
    doc.addModel(std::move(col));
    const EntityId mainId = doc.modelId(0);
    // Generator kullanici parcalarini bilir: bulonlar levha + main (kolon) deler.
    bolts.parts = {mainId};

    ComponentBuilder b;
    b.plate("fin_plate", plate)
        .boltGroup("beam_bolts", bolts)
        .attachedTo("beam_bolts", "fin_plate")
        .ownedBy("fin_plate", "component")
        .ownedBy("beam_bolts", "component");
    const auto built = b.build();
    check(built.ok, "builder ok (levha + bulon grubu)");

    const ComponentPlan plan = componentPlanFromObjects(built.objects, built.relationships);
    check(plan.ok, "plan ok (roleKey korunur)");

    ComponentMaterializer::Prepared prep = mat.prepare(plan, mainId, {});
    check(prep.ok, "prepare ok");
    check(prep.children.size() == 2, "prepare 2 child");

    ComponentInstanceRecord rec;
    rec.typeId = L"fin_plate";
    rec.schemaVersion = 1;
    rec.mainPart = mainId;
    ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
    check(res.ok, "commit ok");
    check(!res.roleToEntity.empty(), "roleToEntity dolu");
    check(res.roleToEntity.count("fin_plate") == 1, "fin_plate -> EntityId");
    check(res.roleToEntity.count("beam_bolts") == 1, "beam_bolts -> EntityId");
    check(res.componentId != kInvalidEntityId, "component id atandi");
    check(res.ownedPlateIds.size() == 1, "1 owned plate");
    check(res.ownedBoltGroupIds.size() == 1, "1 owned bolt group");

    // --- Plate OCCT gecerliligi ---------------------------------------------
    const EntityId plateId = res.roleToEntity["fin_plate"];
    OccSolidStore* store = asSolidStore(&doc.occStore());
    check(store != nullptr, "occ store var");
    if (store) {
        const TopoDS_Shape* ps = store->find(plateId);
        check(ps && !ps->IsNull(), "plate solid var");
        if (ps && !ps->IsNull()) {
            // Levha deliklerle kesilmis (2 bulon x levha): aktif hacim < tam kati.
            const double vol = shapeVolume(*ps);
            check(vol > 0.0 && vol < 120.0 * 240.0 * 10.0,
                  "plate hacmi deliklerle azaldi (0 < vol < tam)");
            // TABAN (pre-cut) surumu TAM levha olmali.
            const VersionId base = store->baseVersion(plateId);
            if (base != kInvalidVersionId) {
                const TopoDS_Shape* baseShape = store->findVersion(plateId, base);
                check(baseShape && !baseShape->IsNull() &&
                          std::abs(shapeVolume(*baseShape) - 120.0 * 240.0 * 10.0) < 1.0,
                      "plate TABAN surumu 120x240x10");
            }
        }
    }

    // --- Bulon grubu + delikler ----------------------------------------------
    const EntityId boltId = res.roleToEntity["beam_bolts"];
    const auto bi = doc.indexOfBoltGroup(boltId);
    check(bi.has_value(), "bolt group index bulundu");
    if (bi) {
        const BoltGroup& bg = doc.boltGroups()[*bi];
        check(bg.parts.size() == 2, "bolt parts = {levha, main}");
        if (bg.parts.size() == 2)
            check(bg.parts[0] == plateId, "bolt parts[0] = levha (sahip olunan)");
    }
    // Delikler: 2 bulon x 2 parca (levha + main) = 4 delik.
    std::size_t holes = 0;
    for (const auto& m : doc.cutModifiers())
        if (m.kind == CutKind::Hole && m.owner == boltId) ++holes;
    check(holes == 4, "4 delik (2 bulon x 2 parca)");

    // --- ComponentInstanceRecord ownership -----------------------------------
    check(doc.componentInstances().size() == 1, "1 component instance");
    const ComponentInstanceRecord& ci = doc.componentInstances()[0];
    check(ci.children.size() == 2, "record.children 2 kayit");
    bool plateChildOk = false, boltChildOk = false;
    for (const auto& c : ci.children) {
        if (c.roleKey == "fin_plate" && c.entityId == plateId) plateChildOk = true;
        if (c.roleKey == "beam_bolts" && c.entityId == boltId) boltChildOk = true;
    }
    check(plateChildOk, "child fin_plate -> EntityId kalici");
    check(boltChildOk, "child beam_bolts -> EntityId kalici");

    // --- Coklu component bagimsiz sahiplenme ----------------------------------
    ComponentBuilder b2;
    PlateSpec p2 = plate;
    p2.width = 60.0;
    b2.plate("fin_plate", p2).ownedBy("fin_plate", "component");
    const auto built2 = b2.build();
    const ComponentPlan plan2 = componentPlanFromObjects(built2.objects, built2.relationships);
    ComponentMaterializer::Prepared prep2 = mat.prepare(plan2, mainId, {});
    ComponentInstanceRecord rec2;
    rec2.typeId = L"fin_plate";
    rec2.mainPart = mainId;
    ComponentMaterializer::CommitResult res2 = mat.commit(prep2, rec2);
    check(res2.ok, "ikinci component commit ok");
    check(doc.componentInstances().size() == 2, "2 component instance");
    const EntityId plate2Id = res2.roleToEntity["fin_plate"];
    check(plate2Id != plateId, "iki levha farkli EntityId (bagimsiz sahiplik)");

    // --- Weld -> acik Unsupported ---------------------------------------------
    ComponentBuilder bw;
    WeldSpec weld;
    weld.throat = 6.0;
    bw.plate("p", plate).weld("w", weld);
    const auto builtW = bw.build();
    const ComponentPlan planW = componentPlanFromObjects(builtW.objects, builtW.relationships);
    ComponentMaterializer::Prepared prepW = mat.prepare(planW, mainId, {});
    check(!prepW.ok, "Weld -> prepare Unsupported (ok=false)");
    check(!prepW.error.empty(), "Weld Unsupported hatasi dolu");

    // --- C3: recompute — roleKey -> EntityId KORUNUR (kimlik stabil) ----------
    {
        // Mevcut ilk component'i (fin_plate + beam_bolts) yeniden hesapla:
        // levha boyutu degisti, ayni roleKey ayni EntityId kalir.
        const EntityId firstCompId = res.componentId;
        const EntityId firstPlateId = plateId;
        const EntityId firstBoltId = boltId;
        const auto compIdx = doc.indexOfComponentInstance(firstCompId);
        check(compIdx.has_value(), "recompute: component bulundu");
        if (compIdx) {
            const ComponentInstanceRecord existing = doc.componentInstances()[*compIdx];

            PlateSpec bigger = plate;
            bigger.width = 140.0;
            bigger.height = 260.0;
            BoltGroup newBolts = bolts;
            ComponentBuilder rb;
            rb.plate("fin_plate", bigger)
                .boltGroup("beam_bolts", newBolts)
                .attachedTo("beam_bolts", "fin_plate")
                .ownedBy("fin_plate", "component")
                .ownedBy("beam_bolts", "component");
            const auto rbuilt = rb.build();
            const ComponentPlan rplan =
                componentPlanFromObjects(rbuilt.objects, rbuilt.relationships);
            ComponentMaterializer::Prepared rprep = mat.prepare(rplan, mainId, {});
            check(rprep.ok, "recompute prepare ok");

            ComponentInstanceRecord newRec;
            newRec.typeId = L"fin_plate";
            newRec.mainPart = mainId;
            ComponentMaterializer::CommitResult rres = mat.recompute(rprep, existing, newRec);
            check(rres.ok, "recompute commit ok");
            check(rres.componentId == firstCompId, "recompute: component id korunur");
            check(rres.roleToEntity["fin_plate"] == firstPlateId,
                  "recompute: fin_plate EntityId KORUNUR");
            check(rres.roleToEntity["beam_bolts"] == firstBoltId,
                  "recompute: beam_bolts EntityId KORUNUR");
            check(doc.componentInstances().size() == 2, "recompute yeni instance EKLEMEDI");

            // Levha yeniden boyutlandi: TABAN surumu yeni 140x260x10.
            OccSolidStore* s = asSolidStore(&doc.occStore());
            const VersionId base = s->baseVersion(firstPlateId);
            if (base != kInvalidVersionId) {
                const TopoDS_Shape* bs = s->findVersion(firstPlateId, base);
                check(bs && !bs->IsNull() &&
                          std::abs(shapeVolume(*bs) - 140.0 * 260.0 * 10.0) < 1.0,
                      "recompute: levha tabani 140x260x10");
            }
        }
    }

    // --- C3: atomic rollback — prepare basarisizsa belge DEGISMEZ -------------
    {
        const std::size_t modelsBefore = doc.models().size();
        const std::size_t compsBefore = doc.componentInstances().size();
        // Dongu: a->b, b->a (AttachedTo) -> prepare Cyclic reddeder.
        ComponentBuilder cyc;
        PlateSpec pp;
        cyc.plate("a", pp).plate("b", pp).attachedTo("a", "b").attachedTo("b", "a");
        const auto cbuilt = cyc.build();
        const ComponentPlan cplan =
            componentPlanFromObjects(cbuilt.objects, cbuilt.relationships);
        ComponentMaterializer::Prepared cprep = mat.prepare(cplan, mainId, {});
        check(!cprep.ok, "dongu -> prepare Cyclic (ok=false)");
        check(doc.models().size() == modelsBefore, "dongu: model eklenmedi (atomik)");
        check(doc.componentInstances().size() == compsBefore,
              "dongu: component eklenmedi (atomik)");
    }

    // --- C3: child removal/creation determinizm -------------------------------
    {
        ComponentChildTable t;
        ComponentChildRecord a;
        a.roleKey = "b_second";
        a.kind = ComponentObjectKind::Plate;
        a.plate = PlateSpec{};
        ComponentChildRecord b2r;
        b2r.roleKey = "a_first";
        b2r.kind = ComponentObjectKind::BoltGroup;
        b2r.boltGroup = BoltGroup{};
        check(t.insert(b2r) && t.insert(a), "child tablo 2 ekle");
        check(t.children()[0].roleKey == "a_first", "child sira a_first");
        check(t.remove("a_first"), "child a_first silindi");
        check(t.size() == 1 && t.find("a_first") == nullptr, "silinen child deterministik yok");
    }

    // --- C4: desteklenmeyen orientation kisiti acik Unsupported ---------------
    {
        ComponentBuilder par;
        PlateSpec pp;
        par.plate("a", pp).plate("b", pp).parallel("a", "b");
        const auto pbuilt = par.build();
        check(pbuilt.ok, "parallel model katmani ok (kaydedilir)");
        const ComponentPlan pplan =
            componentPlanFromObjects(pbuilt.objects, pbuilt.relationships);
        ComponentMaterializer::Prepared pprep = mat.prepare(pplan, mainId, {});
        check(!pprep.ok, "Parallel -> prepare Unsupported (ok=false)");
        check(!pprep.error.empty(), "Parallel Unsupported hatasi dolu");

        ComponentBuilder perp;
        perp.plate("a", pp).plate("b", pp).perpendicular("a", "b");
        const auto perbuilt = perp.build();
        const ComponentPlan perplan =
            componentPlanFromObjects(perbuilt.objects, perbuilt.relationships);
        ComponentMaterializer::Prepared perprep = mat.prepare(perplan, mainId, {});
        check(!perprep.ok, "Perpendicular -> prepare Unsupported (ok=false)");
    }

    // --- C4: ownership/weld/cut iliskileri geometrik dongu olarak COZULMEZ -----
    {
        // Cuts + OwnedBy kenarlari ConstraintSystem bagimliligina aktarilmaz
        // (yapay Cyclic uretilmez) — materializer bu plani gecirmeli.
        ComponentBuilder ow;
        PlateSpec pp;
        pp.width = 100.0;
        pp.height = 100.0;
        pp.thickness = 8.0;
        CutModifier cut;
        cut.kind = CutKind::PlaneCut;
        ow.plate("a", pp).cut("c", cut).cuts("c", "a").ownedBy("a", "component");
        const auto obuilt = ow.build();
        check(obuilt.ok, "cuts+ownedBy model katmani ok");
        const ComponentPlan oplan =
            componentPlanFromObjects(obuilt.objects, obuilt.relationships);
        ComponentMaterializer::Prepared oprep = mat.prepare(oplan, mainId, {});
        check(oprep.ok, "cuts+ownedBy -> prepare ok (dongu DEGIL)");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
