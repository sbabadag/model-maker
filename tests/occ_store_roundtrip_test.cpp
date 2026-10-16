// Minimal OCC store round-trip reproduction: verifies that after save/load
// (serializeActive/Base -> restoreActiveSolid/restoreBaseSolid) the ACTIVE
// solid remains the CUT solid and the BASE solid remains the UNCUT solid.
// Reproduction for: "kayittan sonra kesimler geri gelmiyor".
//
// Derleme:
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_store_roundtrip_test.cpp src/occ_solid_store.cpp \
//       src/occ_brep_serialize.cpp src/geometry.cpp src/cut_modifier.cpp \
//       src/profile_database.cpp src/sha256.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -o occ_store_roundtrip_test && ./occ_store_roundtrip_test

#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>

#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static double vol(const TopoDS_Shape& s) {
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

int main() {
    const EntityId id = 1;
    // Base: 100x100x100 box (vol 1e6). Cut: 50x100x100 box (vol 5e5).
    const TopoDS_Shape baseShape = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Solid();
    const TopoDS_Shape cutShape = BRepPrimAPI_MakeBox(50.0, 100.0, 100.0).Solid();
    const double baseVol = vol(baseShape);
    const double cutVol = vol(cutShape);
    check(baseVol > 9e5 && cutVol < 6e5, "box geometry ok (base ~1e6, cut ~5e5)");

    // 1) Build the store state as the application would: set base, mark base,
    //    then apply cut (new active).
    auto store = makeOccSolidStore();
    OccSolidStore* s = asSolidStore(store.get());
    if (!s) { std::printf("FAIL asSolidStore\n"); return 1; }
    const VersionId vBase = s->set(id, baseShape);
    s->setBaseVersion(id, vBase);
    const VersionId vCut = s->set(id, cutShape);
    check(s->activeVersion(id) == vCut, "active = cut after application");
    check(s->baseVersion(id) == vBase, "base = uncut");

    // 2) Serialize both (what Document::buildMmw8 does).
    const std::string activeBlob = s->serializeActive(id);
    const std::string baseBlob = s->serializeBase(id);
    check(!activeBlob.empty() && !baseBlob.empty(), "both blobs serialized");
    check(activeBlob != baseBlob, "active and base blobs differ");

    // 3) Restore into a FRESH store (what Document::commitFromMmw8 does).
    auto fresh = makeOccSolidStore();
    OccSolidStore* f = asSolidStore(fresh.get());
    std::string err;
    const bool ra = f->restoreActiveSolid(id, activeBlob, &err);
    check(ra, "restoreActiveSolid succeeded");
    const bool rb = f->restoreBaseSolid(id, baseBlob, &err);
    check(rb, "restoreBaseSolid succeeded");

    // 4) THE ASSERTION: active must still be the CUT solid, base the UNCUT.
    const TopoDS_Shape* gotActive = f->find(id);
    check(gotActive != nullptr, "active shape present after load");
    if (gotActive) {
        const double gotVol = vol(*gotActive);
        std::printf("  active vol = %.1f (expect ~%.1f), base vol = %.1f\n",
                    gotVol, cutVol, vol(*f->findVersion(id, f->baseVersion(id))));
        check(std::abs(gotVol - cutVol) < 1.0, "ACTIVE is CUT solid after load (bug: was UNCUT)");
    }
    check(f->baseVersion(id) != kInvalidVersionId, "base version preserved after load");
    if (f->baseVersion(id) != kInvalidVersionId) {
        const TopoDS_Shape* gotBase = f->findVersion(id, f->baseVersion(id));
        check(gotBase && std::abs(vol(*gotBase) - baseVol) < 1.0, "BASE is UNCUT solid after load");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
