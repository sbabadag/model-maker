// FIN PLATE UCTAN UCA (C5) — gercek OCCT kabul testi.
//
// HEDEF: End Plate'e ozel Application kodu KOPYALANMADAN, registry (generateFinPlate)
// -> ComponentPlan (roleKey koprusu) -> ComponentMaterializer -> Document/OCCT
// hatti ile GERCEK Fin Plate (levha + bulon + delikler) uretilir; recompute,
// undo/redo ve MMW8 kayit/ac ayni ORTAK component runtime uzerinden calisir.
//
// Senaryo:
//   HEB300 kolon + IPE200 kiris -> ComponentRegistry "fin_plate" -> plan ->
//   materialize -> OCCT levha + bulon delikleri -> kaydet/ac -> kimlikler korunur
//   -> kiriş tasinir -> recompute ayni EntityId'leri korur -> undo/redo -> acilan
//   component uzerinde tekrar recompute.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_fin_plate_end_to_end_test.cpp \
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
//       src/occ_store_null.cpp src/component_glyph.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase -lTKDESTEP \
//       -o occ_fin_plate_end_to_end_test && ./occ_fin_plate_end_to_end_test

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_glyph.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"
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

// Fin Plate uretimi icin girdi: HEB300 kolon (main) + IPE200 kiris (secondary).
static ComponentInput makeFinPlateInput(const MemberGeometry& col, const MemberGeometry& beam) {
    ComponentInput in;
    in.main = col;
    in.secondary = {beam};
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto path = std::filesystem::temp_directory_path() / "occ_fin_plate_e2e.mmw";

    // --- Profiller + kati geometri ---
    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);

    // Kolon HEB300 (Z ekseni boyunca, +Y flans genisligi).
    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
    // Kiris IPE200 (-Y boyunca, kolon flansina dogru).
    const TopoDS_Shape beamSolid = extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1});

    Document doc;
    doc.setOccStore(makeOccSolidStore());
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) { std::printf("FAIL no occ store\n"); return 1; }

    // Kolon modeli.
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

    // Kiris modeli.
    WireframeModel beamModel = shapeToWireframeWithFaces(beamSolid, 0.15);
    {
        EntityProperties p = beamModel.properties();
        p.profileName = "IPE200";
        p.axisFromX = 0; p.axisFromY = 1000; p.axisFromZ = 1500;
        p.axisToX = 0; p.axisToY = 60; p.axisToZ = 1500;
        p.sectionNormalX = 0; p.sectionNormalY = 0; p.sectionNormalZ = 1;
        beamModel.setProperties(std::move(p));
    }
    doc.addModel(std::move(beamModel));
    const EntityId beamId = doc.modelId(1);
    store->set(beamId, beamSolid);
    store->setBaseVersion(beamId, store->activeVersion(beamId));

    // --- 1. Registry uzerinden Fin Plate sec ---
    const ComponentType* ft = ComponentRegistry::instance().findType(L"fin_plate");
    check(ft != nullptr, "registry fin_plate bulundu");

    // --- 2. generateFinPlate -> GenerationPlan -> ComponentPlan (roleKey koprusu)
    MemberGeometry colGeom;
    colGeom.id = colId;
    colGeom.axisFrom = {0, 0, 0};
    colGeom.axisTo = {0, 0, 3000};
    colGeom.secWidth = 300.0;
    colGeom.secHeight = 300.0;
    MemberGeometry beamGeom;
    beamGeom.id = beamId;
    beamGeom.axisFrom = {0, 1000, 1500};
    beamGeom.axisTo = {0, 60, 1500};
    beamGeom.secWidth = 100.0;
    beamGeom.secHeight = 200.0;

    const GenerationPlan fp = ComponentRegistry::instance().generate(
        L"fin_plate", makeFinPlateInput(colGeom, beamGeom));
    check(fp.ok, "generateFinPlate ok");
    check(fp.plates.size() == 1, "fin plate 1 levha");
    check(fp.boltGroups.size() == 1, "fin plate 1 bulon grubu");

    // --- YERLESIM DOGRULAMA (regresyon): fin plate kolon yuzunde + govdeye paralel.
    // Shear tab levhasi kiris GOVDESINE PARALEL olmali (kalinlik kiris eksenine
    // DIK); eski hata levhayi alin levhasi gibi kirise DIK yerlestirip uzak uca
    // atiyordu. Senaryo: kolon (0,0,0)-(0,0,3000), kiris +Y'den -Y'ye, baglanti
    // ucu kolon +Y yuzunde (Y=150). Varsayilan levha uzunlugu 120 mm.
    {
        const Vec3 beamDir{0.0, -1.0, 0.0};  // axisTo-axisFrom normalize
        const PlateSpec& fpPlate = fp.plates[0];
        const double dotAx = fpPlate.axis.x * beamDir.x + fpPlate.axis.y * beamDir.y +
                             fpPlate.axis.z * beamDir.z;
        check(std::fabs(dotAx) < 1e-6, "fin plate kalinligi kiris eksenine DIK (shear tab)");
        check(std::fabs(fpPlate.center.y - 210.0) < 1e-6,
              "fin plate kolon yuzunde (Y=150) + uzunluk/2");
        check(std::fabs(fpPlate.center.x) < 1e-6 && std::fabs(fpPlate.center.z - 1500.0) < 1e-6,
              "fin plate kiris kotunda (X=0, Z=1500)");
        check(std::fabs(fpPlate.v.z - 1.0) < 1e-6, "fin plate yukseklik yonu +Z (dusey)");
    }

    const ComponentPlan plan = componentPlanFromGenerationPlan(fp);
    check(plan.ok, "ComponentPlan koprusu ok (roleKey korunur)");
    check(plan.children.size() == 2, "plan 2 child (fin_plate + beam_bolts)");

    // --- 3-5. Materialize -> gercek OCCT levha + bulon + sahiplik ---
    ComponentMaterializer mat(doc);
    ComponentMaterializer::Prepared prep = mat.prepare(plan, colId, {beamId});
    check(prep.ok, "prepare ok");

    ComponentInstanceRecord rec;
    rec.typeId = L"fin_plate";
    rec.schemaVersion = 1;
    rec.mainPart = colId;
    rec.secondaryParts = {beamId};
    ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
    check(res.ok, "commit ok");
    const EntityId finPlateId = res.roleToEntity["fin_plate"];
    const EntityId boltGroupId = res.roleToEntity["beam_bolts"];
    check(finPlateId != kInvalidEntityId, "fin_plate -> EntityId");
    check(boltGroupId != kInvalidEntityId, "beam_bolts -> EntityId");

    // Levha kati gercek OCCT.
    const TopoDS_Shape* ps = store->find(finPlateId);
    check(ps && !ps->IsNull() && shapeVolume(*ps) > 0.0, "fin plate OCCT kati uretildi");
    const double plateBaseVol = ps ? shapeVolume(*ps) : 0.0;

    // Delikler: bulon grubu parcalari {levha, kolon} -> 2 bulon x 2 = 4 delik.
    const auto bi = doc.indexOfBoltGroup(boltGroupId);
    check(bi.has_value(), "bolt group kayitli");
    std::size_t holes = 0;
    for (const auto& m : doc.cutModifiers())
        if (m.kind == CutKind::Hole && m.owner == boltGroupId) ++holes;
    check(holes == 4, "4 delik (2 bulon x {levha, kolon})");

    // Sahiplik ComponentInstanceRecord'a yazildi.
    check(doc.componentInstances().size() == 1, "1 component instance");
    const ComponentInstanceRecord& crec = doc.componentInstances()[0];
    check(crec.children.size() == 2, "record children 2");
    check(crec.ownedPlateIds.size() == 1 && crec.ownedBoltGroupIds.size() == 1,
          "owned plate + bolt group kayitli");

    // --- 7b. COMPONENT SEMBOLU (GLYPH): Fin Plate TEK, secilebilir component
    //         olarak tek sembol uretmeli; ankraj levha bbox merkezinde. ---
    const auto glyphsAfterCommit = collectComponentGlyphs(doc);
    check(glyphsAfterCommit.size() == 1, "glyph: tek Fin Plate sembolu");
    Vec3 glyphAnchorBefore{};
    if (!glyphsAfterCommit.empty()) {
        const ComponentGlyph& g = glyphsAfterCommit.front();
        glyphAnchorBefore = g.anchor;
        check(g.entityId == finPlateId, "glyph: sahip olunan levhaya bagli");
        check(g.componentId == crec.id, "glyph: parent component kimligini tasir");
        check(g.universal, "glyph: universal parametrik component");
        bool centerOk = false;
        if (const auto pi = doc.indexOfEntity(finPlateId);
            pi && *pi < doc.modelBounds().size()) {
            const Bounds3& b = doc.modelBounds()[*pi];
            centerOk = std::abs(g.anchor.x - (b.minimum.x + b.maximum.x) * 0.5) < 1e-6 &&
                       std::abs(g.anchor.y - (b.minimum.y + b.maximum.y) * 0.5) < 1e-6 &&
                       std::abs(g.anchor.z - (b.minimum.z + b.maximum.z) * 0.5) < 1e-6;
        }
        check(centerOk, "glyph: levha bbox merkezinde");
    }

    // --- 8. Undo/Redo: tek adimda component tamamen geri alinir ---
    const std::size_t modelsBeforeUndo = doc.models().size();
    check(doc.canUndo(), "undo mevcut (commit tek kayit acar)");
    check(doc.undo(), "undo component olusturmayi geri aldi");
    check(doc.componentInstances().empty(), "undo: component instance silindi");
    check(collectComponentGlyphs(doc).empty(), "glyph: undo sonrasi sembol yok");
    check(doc.models().size() == 2, "undo: levha + bulon display + delikler kaldirildi (kolon+kiris kaldi)");
    check(doc.redo(), "redo component olusturmayi geri getirdi");
    check(doc.componentInstances().size() == 1, "redo: component instance geri geldi");
    check(doc.models().size() == modelsBeforeUndo, "redo: modeller geri geldi");
    check(collectComponentGlyphs(doc).size() == 1, "glyph: redo sonrasi sembol geri geldi");

    // --- 9. MMW8 kaydet/ac ---
    bool saved = true;
    try { doc.save(path); } catch (const std::exception& e) {
        std::printf("FAIL save threw: %s\n", e.what()); saved = false;
    }
    check(saved, "belge kaydedildi");

    Document loaded;
    loaded.setOccStore(makeOccSolidStore());
    bool loadedOk = true;
    try { loaded.load(path); } catch (const std::exception& e) {
        std::printf("FAIL load threw: %s\n", e.what()); loadedOk = false;
    }
    check(loadedOk, "belge yuklendi");

    if (loadedOk) {
        check(loaded.componentInstances().size() == 1, "yuklenen 1 component");
        const ComponentInstanceRecord& lr = loaded.componentInstances()[0];
        check(lr.children.size() == 2, "yuklenen children 2 (roleKey->EntityId korunur)");
        bool plateKeyOk = false, boltKeyOk = false;
        for (const auto& c : lr.children) {
            if (c.roleKey == "fin_plate" && c.entityId == finPlateId) plateKeyOk = true;
            if (c.roleKey == "beam_bolts" && c.entityId == boltGroupId) boltKeyOk = true;
        }
        check(plateKeyOk, "yuklenen plate_0 -> ayni EntityId");
        check(boltKeyOk, "yuklenen bolt_group_0 -> ayni EntityId");

        // Glyph: MMW8 reopen sonrasi sembol tekrar var ve ayni kimliklere bagli.
        const auto loadedGlyphs = collectComponentGlyphs(loaded);
        check(loadedGlyphs.size() == 1, "glyph: MMW8 acilisinda sembol var");
        if (!loadedGlyphs.empty()) {
            check(loadedGlyphs.front().entityId == finPlateId,
                  "glyph: acilis sonrasi ayni levha EntityId");
            check(loadedGlyphs.front().componentId == lr.id,
                  "glyph: acilis sonrasi parent component kimligi korunur");
        }

        // --- 10. Acilan component uzerinde recompute (kiris tasinir) ---
        ComponentMaterializer lmat(loaded);
        OccSolidStore* ls = asSolidStore(&loaded.occStore());
        if (ls) {
            // Kirişi +X yonunde tasi (beam end degisir).
            MemberGeometry movedBeam = beamGeom;
            movedBeam.axisTo = {200, 60, 1500};
            movedBeam.axisFrom = {200, 1000, 1500};
            const GenerationPlan fp2 = ComponentRegistry::instance().generate(
                L"fin_plate", makeFinPlateInput(colGeom, movedBeam));
            check(fp2.ok, "recompute: generateFinPlate (tasinmis kiris) ok");
            const ComponentPlan plan2 = componentPlanFromGenerationPlan(fp2);
            ComponentMaterializer::Prepared prep2 = lmat.prepare(plan2, colId, {beamId});
            check(prep2.ok, "recompute: prepare ok");

            ComponentInstanceRecord newRec;
            newRec.typeId = L"fin_plate";
            newRec.mainPart = colId;
            newRec.secondaryParts = {beamId};
            ComponentMaterializer::CommitResult rres = lmat.recompute(prep2, lr, newRec);
            check(rres.ok, "recompute: commit ok");
            check(rres.roleToEntity["fin_plate"] == finPlateId,
                  "recompute: fin_plate EntityId KORUNUR (acilan doc)");
            check(rres.roleToEntity["beam_bolts"] == boltGroupId,
                  "recompute: beam_bolts EntityId KORUNUR (acilan doc)");
            check(loaded.componentInstances().size() == 1,
                  "recompute: yeni instance EKLEMEDI");

            // Glyph: recompute sonrasi sembol konumu GUNCELLENDI (kiris tasindi).
            const auto movedGlyphs = collectComponentGlyphs(loaded);
            check(movedGlyphs.size() == 1, "glyph: recompute sonrasi tek sembol");
            if (!movedGlyphs.empty()) {
                const Vec3 d{movedGlyphs.front().anchor.x - glyphAnchorBefore.x,
                             movedGlyphs.front().anchor.y - glyphAnchorBefore.y,
                             movedGlyphs.front().anchor.z - glyphAnchorBefore.z};
                check(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) > 1.0,
                      "glyph: recompute sonrasi konum guncellendi");
                check(movedGlyphs.front().componentId ==
                          loaded.componentInstances()[0].id,
                      "glyph: recompute sonrasi parent kimligi korunur");
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
