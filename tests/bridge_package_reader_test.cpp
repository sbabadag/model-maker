// MODEL MAKER BRIDGE — paket okuyucu + esleyici testi (OCC-off).
// Fixture paketini okur, dogrular, esler; ayrica dogrulayicinin kotu paketi
// reddettigini ve guvenli yol kuralini kilitler.

#include "model_maker/bridge_component_mapper.hpp"
#include "model_maker/bridge_package.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

}  // namespace

int main() {
    const std::string dir =
        std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-146-shear-plate.mmbridge";

    // --- okuma + dogrulama ---
    mm::BridgePackage pkg;
    std::vector<std::string> errors;
    const bool ok = mm::readBridgePackage(dir, pkg, errors);
    CHECK(ok);
    if (!ok) {
        for (const auto& e : errors) std::printf("  error: %s\n", e.c_str());
    }

    CHECK(pkg.manifest.sourceComponentNumber == 146);
    CHECK(pkg.manifest.sourceComponentType == "Connection");
    CHECK(pkg.manifest.componentIdentifier == "FIXTURE-146-GUID");
    CHECK(pkg.manifest.formatVersion == "1.0");

    CHECK(pkg.component.number == 146);
    CHECK(pkg.component.primaryMember.guid == "FIXTURE-COL");
    CHECK(pkg.component.primaryMember.profile == "HEA400");
    CHECK(pkg.component.secondaryMembers.size() == 1);
    CHECK(pkg.component.secondaryMembers[0].profile == "IPE300");
    CHECK(pkg.component.coordinateSystem.zAxis.z == 1.0);

    CHECK(pkg.objects.size() == 4);
    CHECK(pkg.parameters.size() == 4);

    // bbox yuklendi mi?
    bool plateHasBbox = false;
    for (const auto& o : pkg.objects)
        if (o.sourceGUID == "FIXTURE-PLATE" && o.bbox.has_value()) plateHasBbox = true;
    CHECK(plateHasBbox);

    // --- esleme ---
    mm::BridgeImportResult res = mm::mapBridgePackage(pkg);
    CHECK(res.ok);
    CHECK(res.supportedCount == 3);    // 2 plate + 1 bolt group
    CHECK(res.unsupportedCount == 1);  // weld

    int plateCount = 0, boltCount = 0, weldCount = 0;
    for (const auto& r : res.records) {
        if (r.mappedKind == "Plate" && r.supported) plateCount++;
        if (r.mappedKind == "BoltGroup" && r.supported) boltCount++;
        if (r.mappedKind == "Weld" && !r.supported) weldCount++;
    }
    CHECK(plateCount == 2);
    CHECK(boltCount == 1);
    CHECK(weldCount == 1);

    // Beam + plaka profili -> Plate eslenir.
    bool stiffMappedToPlate = false;
    for (const auto& r : res.records)
        if (r.sourceGUID == "FIXTURE-STIFF" && r.mappedKind == "Plate") stiffMappedToPlate = true;
    CHECK(stiffMappedToPlate);

    CHECK(!res.previewText.empty());

    // --- V2 paketi: boltGroup / weld / topology / member role ---
    {
        const std::string v2dir =
            std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-146-shear-plate-v2.mmbridge";
        mm::BridgePackage v2;
        std::vector<std::string> v2errs;
        CHECK(mm::readBridgePackage(v2dir, v2, v2errs));
        if (!v2errs.empty())
            for (const auto& e : v2errs) std::printf("  v2err: %s\n", e.c_str());
        CHECK(v2.manifest.formatVersion == "2.0");
        CHECK(v2.component.primaryMember.role == "COLUMN");
        CHECK(v2.component.secondaryMembers.size() == 1 &&
              v2.component.secondaryMembers[0].role == "BEAM");

        const mm::BridgeObject* v2bolt = nullptr;
        const mm::BridgeObject* v2weld = nullptr;
        const mm::BridgeObject* v2plate = nullptr;
        for (const auto& o : v2.objects) {
            if (o.kind == "BoltGroup") v2bolt = &o;
            if (o.kind == "Weld") v2weld = &o;
            if (o.kind == "ContourPlate") v2plate = &o;
        }
        CHECK(v2bolt && v2bolt->boltGroup.has_value());
        if (v2bolt && v2bolt->boltGroup) {
            CHECK(v2bolt->boltGroup->bolts.size() == 6);
            CHECK(v2bolt->boltGroup->size == "M20");
            CHECK(v2bolt->boltGroup->tolerance == 2.0);
            CHECK(v2bolt->boltGroup->axis == "x");
            CHECK(v2bolt->boltGroup->partToBeBolted == "5213");
            CHECK(v2bolt->boltGroup->partToBoltTo == "5673");
        }
        CHECK(v2weld && v2weld->weld.has_value());
        if (v2weld && v2weld->weld) {
            CHECK(v2weld->weld->size == 5.0);
            CHECK(v2weld->weld->mainSourceGUID == "5165");
        }
        CHECK(v2plate && v2plate->topology.has_value());
        if (v2plate && v2plate->topology)
            CHECK(v2plate->topology->faceCount == 6 && v2plate->topology->holeLoops == 0);
    }

    // --- V3 paketi: 146 (relationships + operations + checksums + roleKey) ---
    {
        const std::string v3dir =
            std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-146-shear-plate-v3.mmbridge";
        mm::BridgePackage v3;
        std::vector<std::string> v3errs;
        CHECK(mm::readBridgePackage(v3dir, v3, v3errs));
        for (const auto& e : v3errs) std::printf("  v3err: %s\n", e.c_str());
        CHECK(v3.manifest.formatVersion == "3.0");
        CHECK(v3.manifest.producerName == "ModelMaker.TeklaBridge");
        CHECK(v3.manifest.sourceModelName == "fixture.db1");

        // roleKey + geometryRepresentation + confidence.
        const mm::BridgeObject* v3plate = nullptr;
        for (const auto& o : v3.objects)
            if (o.kind == "ContourPlate") v3plate = &o;
        CHECK(v3plate != nullptr);
        if (v3plate) {
            CHECK(v3plate->roleKey == "shear_tab");
            CHECK(v3plate->geometryRepresentation == "reconstructed");
            CHECK(v3plate->confidence == "observed");
            CHECK(v3plate->bbox.has_value());
        }

        // relationships + operations.
        CHECK(v3.relationships.size() == 9);
        int boltedTo = 0, weldedTo = 0;
        for (const auto& r : v3.relationships) {
            if (r.kind == "boltedTo") ++boltedTo;
            if (r.kind == "weldedTo") ++weldedTo;
        }
        CHECK(boltedTo == 2);
        CHECK(weldedTo == 2);
        CHECK(v3.operations.size() == 1);
        if (!v3.operations.empty()) {
            CHECK(v3.operations[0].type == "BoltHole");
            CHECK(v3.operations[0].targetPartGUID == "5673");
        }
    }

    // --- V3 paketi: 144 cok parcali (mesh + 3 plaka + 2 bulon + 2 weld + fitting) ---
    {
        const std::string dir =
            std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-144-end-plate-v3.mmbridge";
        mm::BridgePackage pkg;
        std::vector<std::string> errs;
        CHECK(mm::readBridgePackage(dir, pkg, errs));
        for (const auto& e : errs) std::printf("  144err: %s\n", e.c_str());

        CHECK(pkg.objects.size() == 9);
        int plates = 0, bolts = 0, welds = 0, fittings = 0, bools = 0;
        const mm::BridgeObject* endPlate = nullptr;
        for (const auto& o : pkg.objects) {
            if (o.kind == "ContourPlate") ++plates;
            if (o.kind == "BoltGroup") ++bolts;
            if (o.kind == "Weld") ++welds;
            if (o.kind == "Fitting") ++fittings;
            if (o.kind == "BooleanPart") ++bools;
            if (o.roleKey == "end_plate") endPlate = &o;
        }
        CHECK(plates == 3);
        CHECK(bolts == 2);
        CHECK(welds == 2);
        CHECK(fittings == 1);
        CHECK(bools == 1);
        CHECK(endPlate && endPlate->mesh.has_value());
        if (endPlate && endPlate->mesh)
            CHECK(endPlate->mesh->vertices.size() == 8 &&
                  endPlate->mesh->triangles.size() == 12);

        // fitting islemi operations.json'da (objects icinde degil).
        int fittingOps = 0;
        for (const auto& op : pkg.operations)
            if (op.type == "Fitting") {
                ++fittingOps;
                CHECK(op.fittingPlane.has_value());
                CHECK(op.inferred);
                CHECK(op.keepSide == "negative");
            }
        CHECK(fittingOps == 1);
    }

    // --- V3 paketi: mesh kutu (mesh->solid yolu veri girisini dogrular) ---
    {
        const std::string dir =
            std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-mesh-box-v3.mmbridge";
        mm::BridgePackage pkg;
        std::vector<std::string> errs;
        CHECK(mm::readBridgePackage(dir, pkg, errs));
        CHECK(pkg.objects.size() == 1);
        if (!pkg.objects.empty()) {
            CHECK(pkg.objects[0].mesh.has_value());
            if (pkg.objects[0].mesh)
                CHECK(pkg.objects[0].mesh->vertices.size() == 8 &&
                      pkg.objects[0].mesh->triangles.size() == 12);
        }
    }

    // --- checksum butunlugu: bozuk dosya reddedilir ---
    {
        const std::string src =
            std::string(MM_BRIDGE_FIXTURE_DIR) + "/fixture-146-shear-plate-v3.mmbridge";
        const std::string bad = src + "-badcs";
        std::filesystem::remove_all(bad);
        std::filesystem::copy(src, bad, std::filesystem::copy_options::recursive);
        // manifest.json'a fazladan bayt ekle (checksum bozulur).
        std::ofstream(bad + "/manifest.json", std::ios::app) << "\n// tamper\n";
        mm::BridgePackage pkg;
        std::vector<std::string> errs;
        const bool ok = mm::readBridgePackage(bad, pkg, errs);
        CHECK(!ok);
        bool foundIntegrity = false;
        for (const auto& e : errs)
            if (e.find("uyusmuyor") != std::string::npos || e.find("butunluk") != std::string::npos)
                foundIntegrity = true;
        CHECK(foundIntegrity);
        std::filesystem::remove_all(bad);
    }

    // --- kotu paket reddedilir (componentIdentifier eksik) ---
    {
        std::string badDir = dir + "-bad";
        std::filesystem::create_directories(badDir);
        // manifest: componentIdentifier bos birakildi
        std::ofstream(badDir + "/manifest.json")
            << "{\"formatVersion\":\"1.0\",\"sourceApplication\":\"Tekla Structures\","
               "\"sourceComponentType\":\"Connection\",\"sourceComponentNumber\":146,"
               "\"exportTimestamp\":\"t\",\"coordinateSystem\":\"TeklaGlobal\","
               "\"lengthUnit\":\"mm\",\"componentIdentifier\":\"\"}";
        std::ofstream(badDir + "/component.json") << "{\"sourceType\":\"Connection\",\"name\":\"x\","
                                                     "\"number\":146,\"guid\":\"G\",\"coordinateSystem\":{},"
                                                     "\"primaryMember\":{\"guid\":\"P\"},\"secondaryMembers\":[]}";
        std::ofstream(badDir + "/parameters.json") << "[]";
        std::ofstream(badDir + "/objects.json") << "[]";
        std::ofstream(badDir + "/diagnostics.json")
            << "{\"knownLimitations\":[],\"inaccessibleObjects\":[],\"unsupportedFeatures\":[],\"warnings\":[]}";

        mm::BridgePackage bad;
        std::vector<std::string> badErrors;
        const bool badOk = mm::readBridgePackage(badDir, bad, badErrors);
        CHECK(!badOk);
        std::filesystem::remove_all(badDir);
    }

    // --- guvenli yol kurali ---
    CHECK(mm::isSafeBridgeRelativePath("geometry/0000.json"));
    CHECK(!mm::isSafeBridgeRelativePath("../evil.json"));
    CHECK(!mm::isSafeBridgeRelativePath("/abs/path.json"));
    CHECK(!mm::isSafeBridgeRelativePath(""));
    CHECK(!mm::isSafeBridgeRelativePath("a//b.json"));

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("bridge_package_reader_test OK\n");
    return 0;
}
