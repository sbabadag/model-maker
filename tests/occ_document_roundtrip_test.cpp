// FULL Document MMW8 round-trip with real OCCT: construction plane + profile
// solid + cut modifier. Reproduces "kayittan sonra plane/kesim geri gelmiyor".
//
// Derleme:
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_document_roundtrip_test.cpp src/document.cpp src/mmw8_codec.cpp \
//       src/occ_solid_store.cpp src/occ_brep_serialize.cpp src/occ_geometry.cpp \
//       src/geometry.cpp src/cut_modifier.cpp src/profile_database.cpp \
//       src/sha256.cpp src/grid.cpp src/occ_store_null.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -o occ_document_roundtrip_test && ./occ_document_roundtrip_test

#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / "occ_doc_roundtrip.mmw";

    // --- Build: document with real OCC store, a profile solid, a construction
    //     plane, and a Fitting cut. ---
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) { std::printf("FAIL no occ store\n"); return 1; }

    // Profile line + properties (as a profile beam would carry them).
    WireframeModel line = WireframeModel::line({0, 0, 0}, {6000, 0, 0});
    EntityProperties p;
    p.profileName = "HEB300";
    p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 0;
    p.axisToX = 6000; p.axisToY = 0; p.axisToZ = 0;
    p.sectionNormalX = 0; p.sectionNormalY = 0; p.sectionNormalZ = 1;
    line.setProperties(std::move(p));
    doc.addModel(std::move(line));
    const EntityId targetId = doc.modelId(0);

    // Construction plane (3-point plane equivalent).
    const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
    const EntityId planeId = doc.modelId(planeIdx);

    // Profile solid in the store.
    SteelProfile heb300;
    heb300.name = "HEB300";
    heb300.height = 300.0; heb300.width = 300.0;
    heb300.plateThickness = 11.0; heb300.flangeThickness = 19.0;
    heb300.roundingRadius = 27.0;
    const TopoDS_Shape baseSolid = extrudeProfileSolid(heb300, {0,0,0}, {6000,0,0});
    store->set(targetId, baseSolid);
    store->setBaseVersion(targetId, store->activeVersion(targetId));

    // Cut modifier (Fitting) referencing the construction plane as source.
    CutModifier fit;
    fit.kind = CutKind::Fitting;
    fit.target = targetId;
    fit.planeOrigin = {2000, 0, 0};
    fit.planeNormal = {1, 0, 0};
    fit.source = planeId;
    fit.enabled = true;
    fit.invertSide = true;
    doc.addCutModifier(fit);

    // Apply the cut (mimic recomputeCutTarget): apply chain, replace mesh, putOccSolid.
    {
        const TopoDS_Shape cut = applyCutModifierChain(baseSolid, {fit}, {0,0,0}, {6000,0,0});
        auto cutModel = shapeToWireframeWithFaces(cut, 0.15);
        EntityProperties cp = doc.models()[0].properties();
        cutModel.setProperties(std::move(cp));
        doc.replaceModel(0, {std::move(cutModel)});
        store->set(targetId, cut);
    }
    const std::size_t cutMeshFaces = doc.models()[0].faces().size();
    check(cutMeshFaces > 0, "cut mesh has faces (target is a solid mesh)");

    // --- Save ---
    bool saved = true;
    try { doc.save(path); } catch (const std::exception& e) {
        std::printf("FAIL save threw: %s\n", e.what()); saved = false;
    }
    check(saved, "document saved");

    // --- Load into a fresh document ---
    Document loaded;
    loaded.setOccStore(makeOccSolidStore());
    bool loadedOk = true;
    try { loaded.load(path); } catch (const std::exception& e) {
        std::printf("FAIL load threw: %s\n", e.what()); loadedOk = false;
    }
    check(loadedOk, "document loaded");

    if (loadedOk) {
        // Construction plane restored?
        const auto cons = loaded.constructionObjects();
        check(cons.size() == 1, "construction plane restored (1 object)");
        if (!cons.empty())
            check(loaded.models()[cons[0]].constructionKind() == ConstructionKind::Plane,
                  "restored construction object is a Plane");

        // Cut modifiers restored?
        check(loaded.cutModifiers().size() == 1, "cut modifier restored (1 modifier)");
        if (!loaded.cutModifiers().empty()) {
            const auto& m = loaded.cutModifiers()[0];
            check(m.kind == CutKind::Fitting, "kind restored");
            check(m.source == planeId, "source EntityId restored");
            check(m.invertSide == true, "invertSide restored");
        }

        // Target mesh still cut (faces present)?
        check(!loaded.models().empty() && loaded.models()[0].faces().size() == cutMeshFaces,
              "target cut mesh restored (face count preserved)");

        // OCC store: active should be CUT, base UNCUT (the regression).
        OccSolidStore* ls = asSolidStore(&loaded.occStore());
        if (ls) {
            const TopoDS_Shape* act = ls->find(targetId);
            const TopoDS_Shape* base = ls->baseVersion(targetId) != kInvalidVersionId
                ? ls->findVersion(targetId, ls->baseVersion(targetId)) : nullptr;
            if (act && base) {
                const double actVol = shapeVolume(*act);
                const double baseVol = shapeVolume(*base);
                std::printf("  after load: active vol %.0f, base vol %.0f\n", actVol, baseVol);
                check(actVol < baseVol, "ACTIVE is CUT (vol < base) after load");
            } else {
                check(false, "active/base solids present after load");
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
