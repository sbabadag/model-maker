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

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
