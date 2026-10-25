// Bridge V3.1 Stage 3C: no automatic fabricated bolt holes from source inference.
// OCC-independent; optional live Tekla 146 package path for extra smoke testing.
#include "model_maker/bridge_drilling_policy.hpp"
#include "model_maker/bridge_package.hpp"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace mm;
namespace {
int failed = 0;
void expect(bool value, const char* label) {
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", label);
    if (!value) ++failed;
}

BridgeBBox plate() {
    return {{4.25, 0, 0}, {14.25, 170, 200}};
}
BridgeBoltGroupInfo measuredBolts() {
    BridgeBoltGroupInfo g;
    g.axis = "x";
    g.axisSource = "observed";
    g.partToBoltTo = "plate-guid";
    g.size = "M20";
    for (double z : {40.0, 100.0, 160.0}) {
        for (double y : {55.0, 115.0}) {
            BridgeBolt b;
            b.position = {5, y, z};
            b.nominalDiameter = 20.0;
            b.holeDiameter = 22.0;
            b.holeDiameterSource = "measured";
            g.bolts.push_back(b);
        }
    }
    return g;
}

void testLive(const std::string& dir) {
    BridgePackage pkg;
    std::vector<std::string> errors;
    const bool loaded = readBridgePackage(dir, pkg, errors);
    expect(loaded, "live package C++ reader passed");
    if (!loaded) {
        for (const auto& e : errors) std::printf("  error: %s\n", e.c_str());
        return;
    }
    const BridgeObject* p = nullptr;
    const BridgeObject* bg = nullptr;
    for (const auto& o : pkg.objects) {
        if (o.kind == "ContourPlate" && o.bbox) p = &o;
        if (o.kind == "BoltGroup" && o.boltGroup) bg = &o;
    }
    expect(p != nullptr && bg != nullptr, "live plate and boltGroup exist");
    if (!p || !bg) return;
    const auto decision = authorizeBridgeAutoDrill(*bg->boltGroup, *p->bbox, p->sourceGUID);
    std::printf("LIVE Stage3C decision: allowed=%s; %s\n",
                decision.allowed ? "true" : "false", decision.reason.c_str());
    // The live package has unresolved axis and unverified diameter.
    expect(!decision.allowed, "live Tekla 146 does not authorize physical drilling");
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fputs("Usage: model_maker_bridge_auto_drill_policy_test [live-dir]\n", stderr);
        return 2;
    }
    const BridgeBBox box = plate();

    {
        const auto d = authorizeBridgeAutoDrill(measuredBolts(), box, "plate-guid");
        expect(d.allowed, "fully observed six M20 holes permitted");
    }
    {
        auto g = measuredBolts(); g.axis.clear();
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "missing axis blocks physical drilling");
    }
    {
        auto g = measuredBolts(); g.axisSource = "inferred";
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "bbox-inferred axis blocks physical drilling");
    }
    {
        auto g = measuredBolts(); g.bolts[0].holeDiameter = 0.0;
        g.bolts[0].holeDiameterSource = "standard";
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "standard-guess diameter cannot count as observed");
    }
    {
        auto g = measuredBolts(); g.bolts[0].position.x = 0.0;
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "Tekla X=0 versus plate X=4.25..14.25 must not silently re-center");
    }
    {
        auto g = measuredBolts();
        expect(!authorizeBridgeAutoDrill(g, box, "wrong-guid").allowed,
               "incorrect target plate GUID blocks drilling");
    }
    {
        auto g = measuredBolts(); g.bolts[0].position.y = 5.0;
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "hole too close to the plate edge blocks drilling");
    }
    {
        auto g = measuredBolts();
        g.bolts[0].position.z = std::numeric_limits<double>::quiet_NaN();
        expect(!authorizeBridgeAutoDrill(g, box, "plate-guid").allowed,
               "non-finite source coordinates block drilling");
    }
    if (argc == 2) testLive(argv[1]);
    return failed ? 1 : 0;
}
