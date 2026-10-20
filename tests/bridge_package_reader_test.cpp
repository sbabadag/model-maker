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
