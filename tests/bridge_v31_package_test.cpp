// Bridge V3.1 Stage 2: real legacy-adapter output against the existing
// Model Maker C++ Bridge V3 reader and semantic preview mapper.
// No OCCT/B-Rep or editable component equivalence is claimed.
#include "model_maker/bridge_component_mapper.hpp"
#include "model_maker/bridge_package.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef MM_BRIDGE_V31_EXAMPLES_DIR
#error "MM_BRIDGE_V31_EXAMPLES_DIR is required"
#endif

namespace {
namespace fs = std::filesystem;
int failures = 0;

void require(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void verifyExample(const char* folder, int expectedNumber,
                   std::size_t expectedObjects, std::size_t expectedParameters,
                   std::size_t expectedUnsupported) {
    const fs::path dir = fs::path(MM_BRIDGE_V31_EXAMPLES_DIR) / folder;
    const std::string prefix = std::string(folder) + ": ";
    require(fs::is_regular_file(dir / "checksums.json"),
            prefix + "checksums.json must be present");
    std::vector<std::string> checksumErrors;
    require(mm::verifyBridgeChecksums(dir.string(), checksumErrors),
            prefix + "SHA-256 checksums must pass");
    for (const auto& e : checksumErrors)
        std::printf("  checksum error: %s\n", e.c_str());

    mm::BridgePackage pkg;
    std::vector<std::string> errors;
    const bool ok = mm::readBridgePackage(dir.string(), pkg, errors);
    require(ok, prefix + "C++ readBridgePackage() failed");
    if (!ok) {
        for (const auto& e : errors)
            std::printf("  reader error: %s\n", e.c_str());
        return;  // Never use a rejected BridgePackage.
    }

    require(pkg.manifest.formatVersion == "3.0", prefix + "formatVersion");
    require(pkg.manifest.sourceComponentNumber == expectedNumber,
            prefix + "source component number");
    require(pkg.component.number == expectedNumber, prefix + "component number");
    require(pkg.manifest.componentIdentifier == pkg.component.guid,
            prefix + "component identifier consistency");
    require(pkg.objects.size() == expectedObjects, prefix + "object count");
    require(pkg.parameters.size() == expectedParameters, prefix + "parameter count");
    require(pkg.manifest.exportedObjectCount == static_cast<int>(expectedObjects),
            prefix + "manifest exported count");
    require(pkg.unsupportedFeatures.size() == expectedUnsupported,
            prefix + "unsupported diagnostics retained");

    // All these legacy probes must remain reference/unverified, not exact
    // reconstructed parametric geometry.
    bool hasReference = false;
    for (const auto& o : pkg.objects) {
        require(!o.sourceGUID.empty(), prefix + "empty object identifier");
        require(!o.roleKey.empty(), prefix + "missing roleKey");
        if (o.geometryRepresentation == "reference") hasReference = true;
    }
    require(hasReference, prefix + "reference geometry status lost");

    const mm::BridgeImportResult preview = mm::mapBridgePackage(pkg);
    require(preview.ok, prefix + "semantic preview failed");
    require(preview.records.size() == expectedObjects,
            prefix + "semantic record count");
    require(preview.supportedCount + preview.unsupportedCount ==
                static_cast<int>(expectedObjects),
            prefix + "supported/unsupported accounting");

    if (expectedNumber == 144 || expectedNumber == 146) {
        const std::size_t expectedBoltCount = expectedNumber == 144 ? 8 : 6;
        std::size_t foundBoltCount = 0;
        for (const auto& o : pkg.objects) {
            if (o.kind != "BoltGroup" || !o.boltGroup) continue;
            foundBoltCount += o.boltGroup->bolts.size();
            // Hole/axis are NOT observed in these legacy probes.
            require(o.boltGroup->axis.empty(), prefix + "fabricated bolt axis");
            for (const auto& b : o.boltGroup->bolts) {
                require(b.nominalDiameter == 20.0, prefix + "bolt diameter");
                require(b.holeDiameter == 0.0, prefix + "fabricated hole diameter");
            }
        }
        require(foundBoltCount == expectedBoltCount, prefix + "bolt positions lost");
    }
    if (expectedNumber == 146) {
        require(pkg.component.guid == "probe-id:5245",
                prefix + "legacy probe ID provenance");
        bool foundPlate = false;
        for (const auto& o : pkg.objects)
            if (o.kind == "ContourPlate" && o.bbox &&
                o.geometryRepresentation == "reference" &&
                o.topology && o.topology->faceCount == 6)
                foundPlate = true;
        require(foundPlate, prefix + "plate bbox/topology/reference missing");
    }
    if (expectedNumber == 40) {
        require(pkg.component.guid == "probe-id:990503",
                prefix + "legacy probe ID provenance");
        require(pkg.manifest.coordinateSystem == "unverified",
                prefix + "unverified coordinate warning lost");
    }
    std::printf("PASS: %s (component %d; objects %zu; parameters %zu; unsupported %zu)\n",
                folder, expectedNumber, pkg.objects.size(),
                pkg.parameters.size(), pkg.unsupportedFeatures.size());
}
}  // namespace

int main() {
    verifyExample("end-plate-144-v3", 144, 3, 10, 3);
    verifyExample("real-146-v3", 146, 8, 0, 7);
    verifyExample("haunch-40-v3", 40, 67, 0, 57);
    if (failures != 0) {
        std::printf("Bridge V3.1 C++ reader: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("Bridge V3.1 C++ reader: 3/3 packages PASS (no OCCT equivalence claim)");
    return 0;
}
