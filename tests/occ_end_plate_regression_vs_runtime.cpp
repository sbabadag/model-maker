// END PLATE REGRESYON: mevcut production hatti vs yeni ortak runtime (A3).
//
// AMAC: End Plate migration'dan ONCE, yeni ortak ComponentMaterializer runtime'in
// mevcut production davranisini (geometri, bulon, delik, ownership) YENIDEN
// URETTIGINI kanitlamak. Ayni yerlesim (EndPlatePlacement) iki yoldan beslenir:
//
//   (A) MEVCUT: buildEndPlateFromPlacement + Document::addEndPlateConnection
//   (B) YENI  : ComponentBuilder -> componentPlanFromObjects -> ComponentMaterializer
//               -> commit (registry hatti ile AYNI runtime)
//
// Karsilastirilan:
//   - levha B-Rep hacmi (geometri esdegerligi)
//   - bulon adedi (8 = 2x4)
//   - delik (CutKind::Hole) adedi + hedef parcalar (kolon + levha)
//   - ownership: component mainPart, owned plate, owned bolt group
//
// NOT: Fitting kesmesi (kiris ucunun levha arka yuzune kesilmesi) Application
// katmaninda eklenir (generateEndPlate uretmez) — burada "mevcut yol fitting
// ekler, yeni runtime fitting'i Cut child olarak kabul eder" ayri dogrulanir.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_end_plate_regression_vs_runtime.cpp \
//       src/component_materializer.cpp src/component_framework.cpp \
//       src/component_engine.cpp src/document.cpp src/mmw8_codec.cpp \
//       src/end_plate.cpp src/bolt.cpp src/cut_modifier.cpp src/geometry.cpp \
//       src/profile_database.cpp src/reference_geometry.cpp \
//       src/constraint_system.cpp src/contact_resolver.cpp \
//       src/geometry_reference_resolver.cpp src/magnetic_frame.cpp \
//       src/component_recompute.cpp src/component.cpp src/assembly.cpp \
//       src/camera.cpp src/occ_contact_resolver.cpp src/occ_solid_store.cpp \
//       src/occ_brep_serialize.cpp src/occ_geometry.cpp src/bolt_geometry.cpp \
//       src/end_plate_geometry.cpp src/sha256.cpp src/grid.cpp \
//       src/occ_store_null.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase -lTKDESTEP \
//       -o occ_end_plate_regression_vs_runtime && ./occ_end_plate_regression_vs_runtime

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_contact_resolver.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/end_plate.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/profile_database.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }

static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

// Ayni yerlesimden hem mevcut hem yeni yolla End Plate uretir; karsilastirir.
int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SectionDims dims300{300.0, 300.0, 11.0, 19.0};

    EndPlateParams params;  // M20/22, 12 mm levha, 2x4 bulon
    params.gap = 0.0;
    params.boltCountX = 2;
    params.boltCountY = 4;

    const auto colFrame = makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}),
                                               {0, 0, 0});
    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0,
                                                      {0, 1, 0});
    const Vec3 beamEnd{0, 500, 1500};
    const Vec3 beamDir{0, -1, 0};
    ContactPlacement contact = resolveOcctContact(colSolid, colFrame, dims300, 3000, beamEnd,
                                                  beamDir, SemanticFaceRole::FlangeOuterPositive);
    check(contact.status == ResolutionStatus::Resolved, "temas cozuldu");
    ReferencePlane face;
    face.origin = contact.contactPoint;
    face.normal = contact.outwardNormal;
    EndPlatePlacement placement =
        computeEndPlatePlacementOnFace(face, params.gap, 100.0, 200.0, params);
    check(boltGroupBoltOrigins(placement.bolts).size() == 8, "2x4 = 8 bulon");

    // ================== (A) MEVCUT PRODUCTION HATTI ===========================
    Document docA;
    docA.setOccStore(makeOccSolidStore());
    OccSolidStore* storeA = asSolidStore(&docA.occStore());
    if (!storeA) { std::printf("FAIL no occ store A\n"); return 1; }

    WireframeModel colA = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colA.properties();
        p.profileName = "HEB300";
        p.axisFromZ = 0; p.axisToZ = 3000;
        p.sectionNormalY = 1;
        colA.setProperties(std::move(p));
    }
    docA.addModel(std::move(colA));
    const EntityId colAId = docA.modelId(0);
    storeA->set(colAId, colSolid);
    storeA->setBaseVersion(colAId, storeA->activeVersion(colAId));

    EndPlateConnection conn;
    conn.mainPart = colAId;
    conn.params = params;
    conn.bolts = placement.bolts;
    conn.enabled = true;
    const TopoDS_Shape plateSolidA = buildEndPlateFromPlacement(placement);
    const TopoDS_Shape boltSolidA =
        buildBoltGroupAssemblyShape(placement.bolts, placement.plateThickness + 15.0);
    check(!plateSolidA.IsNull() && shapeVolume(plateSolidA) > 0.0, "A: levha B-Rep uretildi");
    const std::size_t ciA = docA.addEndPlateConnection(
        std::move(conn), shapeToWireframeWithFaces(plateSolidA, 0.15),
        shapeToWireframeWithFaces(boltSolidA, 0.15));
    const EndPlateConnection& createdA = docA.endPlateConnections()[ciA];
    storeA->set(createdA.plateEntityId, plateSolidA);
    storeA->set(createdA.bolts.displayEntityId, boltSolidA);

    const double plateVolA = shapeVolume(plateSolidA);
    std::size_t holesA = 0;
    for (const auto& m : docA.cutModifiers())
        if (m.kind == CutKind::Hole) ++holesA;
    check(holesA == 16, "A: 16 delik (8 bulon x kolon+levha)");

    // ================== (B) YENI ORTAK RUNTIME ===============================
    Document docB;
    docB.setOccStore(makeOccSolidStore());
    OccSolidStore* storeB = asSolidStore(&docB.occStore());
    if (!storeB) { std::printf("FAIL no occ store B\n"); return 1; }
    ComponentMaterializer matB(docB);

    WireframeModel colB = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colB.properties();
        p.profileName = "HEB300";
        p.axisFromZ = 0; p.axisToZ = 3000;
        p.sectionNormalY = 1;
        colB.setProperties(std::move(p));
    }
    docB.addModel(std::move(colB));
    const EntityId colBId = docB.modelId(0);
    storeB->set(colBId, colSolid);
    storeB->setBaseVersion(colBId, storeB->activeVersion(colBId));

    // Ayni yerlesimden PlateSpec + BoltGroup kur (registry hattiyla AYNI runtime'a
    // beslenir — ComponentBuilder yolu).
    PlateSpec plateSpec;
    plateSpec.center = placement.plateCenter;
    plateSpec.u = placement.u;
    plateSpec.v = placement.v;
    plateSpec.axis = placement.axis;
    plateSpec.width = placement.plateWidth;
    plateSpec.height = placement.plateHeight;
    plateSpec.thickness = placement.plateThickness;
    plateSpec.name = L"Alin Levhasi";

    BoltGroup bolts = placement.bolts;
    bolts.parts = {colBId};

    ComponentBuilder b;
    b.plate("end_plate", plateSpec)
        .boltGroup("beam_bolts", bolts)
        .ownedBy("end_plate", "component")
        .ownedBy("beam_bolts", "component");
    const auto built = b.build();
    check(built.ok, "B: builder ok");
    const ComponentPlan plan = componentPlanFromObjects(built.objects, built.relationships);
    check(plan.ok, "B: plan ok");
    ComponentMaterializer::Prepared prep = matB.prepare(plan, colBId, {});
    check(prep.ok, "B: prepare ok");
    ComponentInstanceRecord recB;
    recB.typeId = L"end_plate";
    recB.mainPart = colBId;
    ComponentMaterializer::CommitResult resB = matB.commit(prep, recB);
    check(resB.ok, "B: commit ok");

    // ================== KARSILASTIRMA ========================================
    const EntityId plateIdB = resB.roleToEntity["end_plate"];
    const EntityId boltIdB = resB.roleToEntity["beam_bolts"];
    check(plateIdB != kInvalidEntityId && boltIdB != kInvalidEntityId,
          "B: levha + bulon EntityId atandi");
    check(resB.ownedPlateIds.size() == 1, "B: 1 owned levha");
    check(resB.ownedBoltGroupIds.size() == 1, "B: 1 owned bulon grubu");

    // Geometri esdegerligi: levha hacmi (delik ONCESI taban B-Rep).
    const VersionId plateBaseB = storeB->baseVersion(plateIdB);
    const TopoDS_Shape* plateBaseShapeB = storeB->findVersion(plateIdB, plateBaseB);
    check(plateBaseShapeB && !plateBaseShapeB->IsNull(), "B: levha taban B-Rep var");
    const double plateVolB = plateBaseShapeB ? shapeVolume(*plateBaseShapeB) : 0.0;
    check(approx(plateVolB, plateVolA, 1e-2), "levha hacmi ESIT (mevcut vs yeni)");

    // Bulon adedi: ayni grid (2x4 = 8).
    const auto bi = docB.indexOfBoltGroup(boltIdB);
    check(bi.has_value(), "B: bulon grubu document'ta");
    if (bi) {
        check(boltGroupBoltOrigins(docB.boltGroups()[*bi]).size() == 8,
              "bulon adedi ESIT (8)");
    }

    // Delik adedi + hedef: kolon + levha (8'er).
    std::size_t holesOnColB = 0, holesOnPlateB = 0;
    for (const auto& m : docB.cutModifiers()) {
        if (m.kind != CutKind::Hole) continue;
        if (m.target == colBId) ++holesOnColB;
        if (m.target == plateIdB) ++holesOnPlateB;
    }
    check(holesOnColB == 8, "B: kolonda 8 delik");
    check(holesOnPlateB == 8, "B: levhada 8 delik");
    check(holesOnColB + holesOnPlateB == holesA, "toplam delik sayisi ESIT (16)");

    // Ownership: component kaydi mainPart + owned id'ler dogru.
    check(docB.componentInstances().size() == 1, "B: 1 component instance");
    check(docB.componentInstances()[0].mainPart == colBId, "B: mainPart == kolon");
    check(docB.componentInstances()[0].ownedPlateIds.size() == 1, "B: ownedPlateIds 1");
    check(docB.componentInstances()[0].ownedBoltGroupIds.size() == 1, "B: ownedBoltGroupIds 1");

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
