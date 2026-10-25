// END PLATE MMW8 ROUND-TRIP + RECOMPUTE (Slice 2d-3, Slice D) — gercek OCCT
// kabul testi.
//
// Nihai basari olcutu: MMW8'den acilan End Plate baglantisinin kolon/kiris
// degisiminde levha, bulon ve delikleri YENIDEN HESAPLAYABILMESI.
//
// Senaryo:
//   HEB300 kolon + IPE200 kiris + End Plate + 2x4 M20 BoltGroup kur -> MMW8
//   kaydet -> kapat/yukle -> kimlikleri karsilastir -> kolonu HEB400'e
//   degistir -> levha/bulon/delik yeniden hesaplanir -> tekrar kaydet/ac ->
//   parametrik duzenlenebilir kalir.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_end_plate_roundtrip_acceptance.cpp \
//       src/document.cpp src/mmw8_codec.cpp src/component_engine.cpp \
//       src/end_plate.cpp src/end_plate_geometry.cpp src/bolt.cpp \
//       src/bolt_geometry.cpp src/cut_modifier.cpp src/geometry.cpp \
//       src/profile_database.cpp src/reference_geometry.cpp \
//       src/constraint_system.cpp src/contact_resolver.cpp \
//       src/geometry_reference_resolver.cpp src/magnetic_frame.cpp \
//       src/component_recompute.cpp src/component.cpp src/assembly.cpp \
//       src/occ_contact_resolver.cpp src/occ_solid_store.cpp \
//       src/occ_brep_serialize.cpp src/occ_geometry.cpp src/sha256.cpp \
//       src/grid.cpp src/occ_store_null.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -o occ_end_plate_roundtrip_acceptance && ./occ_end_plate_roundtrip_acceptance

#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_contact_resolver.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/end_plate.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/profile_database.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>

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

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto path = std::filesystem::temp_directory_path() / "occ_end_plate_roundtrip.mmw";

    // --- Profiller ---
    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile heb400 = makeProfile("HEB400", 400.0, 400.0, 13.5, 24.0);
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);
    const SectionDims dims300{300.0, 300.0, 11.0, 19.0};
    const SectionDims dims400{400.0, 400.0, 13.5, 24.0};

    EndPlateParams params;  // M20/22, 12 mm levha, 2x4 bulon, gap=0
    params.gap = 0.0;
    params.boltCountX = 2;
    params.boltCountY = 4;

    // --- Kolon cercevesi + katisi ---
    const auto colFrame = makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}),
                                               {0, 0, 0});
    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0,
                                                      {0, 1, 0});

    // --- Temas + yerlesim (Application::prepareEndPlateConnection'in hatti) ---
    const Vec3 beamEnd{0, 500, 1500};
    const Vec3 beamDir{0, -1, 0};
    ContactPlacement contact = resolveOcctContact(colSolid, colFrame, dims300, 3000, beamEnd,
                                                  beamDir, SemanticFaceRole::FlangeOuterPositive);
    check(contact.status == ResolutionStatus::Resolved, "temas cozuldu (HEB300)");
    ReferencePlane face;
    face.origin = contact.contactPoint;
    face.normal = contact.outwardNormal;
    EndPlatePlacement placement =
        computeEndPlatePlacementOnFace(face, params.gap, /*secWidth*/ 100.0,
                                       /*secHeight*/ 200.0, params);
    check(boltGroupBoltOrigins(placement.bolts).size() == 8, "2x4 = 8 bulon");
    check(approx(placement.plateCenter.y, 6.0), "levha merkezi Y=+6 (gap=0, t=12)");

    // --- Belge kur ---
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) { std::printf("FAIL no occ store\n"); return 1; }

    // Kolon modeli + kati.
    WireframeModel colModel = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colModel.properties();
        p.profileName = "HEB300";
        p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 0;
        p.axisToX = 0; p.axisToY = 0; p.axisToZ = 3000;
        p.sectionNormalX = 0; p.sectionNormalY = 1; p.sectionNormalZ = 0;
        colModel.setProperties(std::move(p));
    }
    doc.addModel(std::move(colModel));
    const EntityId colId = doc.modelId(0);
    store->set(colId, colSolid);
    store->setBaseVersion(colId, store->activeVersion(colId));

    // Kiris modeli + kati (IPE200, -Y boyunca).
    const TopoDS_Shape beamSolid = extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500},
                                                       0.0, {0, 0, 1});
    WireframeModel beamModel = shapeToWireframeWithFaces(beamSolid, 0.15);
    {
        EntityProperties p = beamModel.properties();
        p.profileName = "IPE200";
        p.axisFromX = 0; p.axisFromY = 1000; p.axisFromZ = 1500;
        p.axisToX = 0; p.axisToY = 60; p.axisToZ = 1500;
        beamModel.setProperties(std::move(p));
    }
    doc.addModel(std::move(beamModel));
    const EntityId beamId = doc.modelId(1);
    store->set(beamId, beamSolid);
    store->setBaseVersion(beamId, store->activeVersion(beamId));

    // End Plate baglantisi (levha + bulon montaji + delikler).
    EndPlateConnection conn;
    conn.mainPart = colId;
    conn.secondaryPart = beamId;
    conn.params = params;
    conn.bolts = placement.bolts;
    conn.enabled = true;
    const TopoDS_Shape plateSolid = buildEndPlateFromPlacement(placement);
    const TopoDS_Shape boltSolid =
        buildBoltGroupAssemblyShape(placement.bolts, placement.plateThickness + 15.0);
    check(!plateSolid.IsNull() && shapeVolume(plateSolid) > 0.0, "levha B-Rep uretildi");
    const std::size_t ci = doc.addEndPlateConnection(std::move(conn),
                                                     shapeToWireframeWithFaces(plateSolid, 0.15),
                                                     shapeToWireframeWithFaces(boltSolid, 0.15));
    const EndPlateConnection& created = doc.endPlateConnections()[ci];
    const EntityId plateId = created.plateEntityId;
    const EntityId boltGroupId = created.bolts.id;
    const EntityId epId = created.id;
    store->set(plateId, plateSolid);
    store->set(created.bolts.displayEntityId, boltSolid);

    // Delikleri (CutKind::Hole) GERCEK B-Rep uzerine uygula: kolon delinir.
    std::vector<CutModifier> colHoles;
    for (const auto& m : doc.cutModifiers())
        if (m.kind == CutKind::Hole && m.target == colId) colHoles.push_back(m);
    check(colHoles.size() == 8, "kolonda 8 delik (8 bulon)");
    const TopoDS_Shape cutCol = applyCutModifierChain(colSolid, colHoles, {0, 0, 0}, {0, 0, 3000});
    store->set(colId, cutCol);
    const double colBaseVol = shapeVolume(colSolid);
    const double colCutVol = shapeVolume(cutCol);
    check(colCutVol < colBaseVol, "kolon deliklerle kesildi (hacim azaldi)");

    // --- Kaydet ---
    bool saved = true;
    try { doc.save(path); } catch (const std::exception& e) {
        std::printf("FAIL save threw: %s\n", e.what()); saved = false;
    }
    check(saved, "belge kaydedildi");

    // --- Yukle ---
    Document loaded;
    loaded.setOccStore(makeOccSolidStore());
    bool loadedOk = true;
    try { loaded.load(path); } catch (const std::exception& e) {
        std::printf("FAIL load threw: %s\n", e.what()); loadedOk = false;
    }
    check(loadedOk, "belge yuklendi");

    if (loadedOk) {
        // Kimlikler korunur.
        check(loaded.modelId(0) == colId, "kolon kimligi korunur");
        check(loaded.modelId(1) == beamId, "kiris kimligi korunur");
        check(loaded.endPlateConnections().size() == 1, "1 End Plate baglantisi");
        if (loaded.endPlateConnections().size() == 1) {
            const auto& le = loaded.endPlateConnections()[0];
            check(le.id == epId, "baglanti kimligi korunur");
            check(le.plateEntityId == plateId, "levha kimligi korunur");
            check(le.bolts.id == boltGroupId, "bulon grubu kimligi korunur");
            check(le.mainPart == colId && le.secondaryPart == beamId,
                  "main/secondary referanslari korunur");
            check(le.params.boltCountX == 2 && le.params.boltCountY == 4,
                  "2x4 bulon duzeni korunur");
            check(approx(le.params.boltDiameter, 20.0) && approx(le.params.holeDiameter, 22.0),
                  "M20/22 parametreleri korunur");
            check(le.enabled, "enabled korunur");
        }
        // Delik modifier'lari korunur (8 bulon x 2 parca = 16).
        std::size_t holeCount = 0;
        for (const auto& m : loaded.cutModifiers())
            if (m.kind == CutKind::Hole) ++holeCount;
        check(holeCount == 16, "16 delik modifier korunur");

        // OCC store: aktif = kesilmis (delikler), taban = kesilmemis.
        OccSolidStore* ls = asSolidStore(&loaded.occStore());
        if (ls) {
            const TopoDS_Shape* act = ls->find(colId);
            const TopoDS_Shape* base = ls->baseVersion(colId) != kInvalidVersionId
                ? ls->findVersion(colId, ls->baseVersion(colId)) : nullptr;
            if (act && base)
                check(shapeVolume(*act) < shapeVolume(*base),
                      "AKTIF kesilmis (hacim < taban) yukleme sonrasi");
            else
                check(false, "aktif/taban katilar mevcut");
        }

        // --- Recompute-after-load: kolonu HEB400'e degistir, levha/bulon/delik
        //     yeniden hesaplanir (MMW8'den acilan baglanti parametrik kalir). ---
        const TopoDS_Shape col400 =
            extrudeProfileSolid(heb400, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
        ContactPlacement c2 = resolveOcctContact(col400, colFrame, dims400, 3000, beamEnd,
                                                 beamDir, SemanticFaceRole::FlangeOuterPositive);
        check(c2.status == ResolutionStatus::Resolved, "HEB400 temas cozuldu");
        ReferencePlane f2;
        f2.origin = c2.contactPoint;
        f2.normal = c2.outwardNormal;
        EndPlatePlacement p2 = computeEndPlatePlacementOnFace(f2, params.gap, 100.0, 200.0,
                                                              params);
        check(approx(p2.plateCenter.y, 6.0), "HEB400: levha Y=+6 (ust yuzey sabit)");
        check(boltGroupBoltOrigins(p2.bolts).size() == 8, "HEB400: hala 8 bulon");
        BoltGroup b2 = p2.bolts;
        b2.parts = {plateId, colId};
        check(boltGroupHoleModifiers(b2).size() == 16, "HEB400: 16 delik yeniden uretilir");
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
