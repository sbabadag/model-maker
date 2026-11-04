// ============================================================================
// MM-4b (D) — Haunch 40: simetrik mertegin 180° roll duyarsizligi.
//
// Studio ST-4·2 bulgusu (42f61ee): HEA300 gibi CIFT-SIMETRIK mertek, kendi
// ekseni etrafinda 180° roll edilince (ayni fiziksel kati) Haunch 40 plani
// degisiyordu — mertek profileRotation 180 olan 6 vakanin 6'si kirmizi.
// Kok neden iki katmanliydi:
//
//   (1) KANONIK KATMAN: memberGeometryFromProfile cift-simetrik kesitte 180°
//       roll'u [0,180) yerine [0,360)'a indiriyordu; 180° roll 0°'ye esit
//       olmadigi icin ayni fiziksel uye farkli MemberGeometry veriyordu.
//   (2) GENERATOR KATMANI: generateHaunch40 "alt tarafi" (haunch konumu) ve
//       bolon sirasini kesit cercevesinin isaretine (g.H) gore belirliyordu;
//       180° roll g.H'yi ters cevirip haunch'i USTE tasiyordu.
//
// Bu test IKI sartlanmayi da kilitler:
//   A. KANONIK: cift-simetrik kesitte 180->0, 270->90 (same MemberGeometry);
//      asimetrik kesitte 180 korunur (ayna goruntusu uyedir).
//   B. GENERATOR: her golden girdi icin roll-180 varyanti (profileRotation=180)
//      ile roll-0 AYNI plani verir (GEOMETRIK 1e-9 + digest esitligi).
// ============================================================================
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s\n", msg);                                    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kEps = 1e-9;

static bool near(double a, double b) { return std::fabs(a - b) <= kEps; }
static bool nearV(const Vec3& a, const Vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

// --- golden girdiler (haunch40_equivalence_test ile ayni) --------------------

static ComponentInput makeInput(double slopeDeg) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 1, 0};
    in.main.secHeight = 300;
    in.main.secWidth = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;
    in.main.sectionCenterOffset = 0.0;

    MemberGeometry beam;
    beam.id = 2;
    const double t = std::tan(slopeDeg * kPi / 180.0);
    beam.axisFrom = {0, 1200, 1500 + 1140 * t};
    beam.axisTo = {0, 60, 1500};
    beam.refNormal = {0, 0, 1};
    beam.secHeight = 200;
    beam.secWidth = 100;
    beam.secWebThickness = 5.6;
    beam.secFlangeThickness = 8.5;
    beam.sectionCenterOffset = 0.0;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputRotatedColumn() {
    ComponentInput in;
    in.main.id = 45;
    in.main.axisFrom = {-115.0, 12000.0, 0.0};
    in.main.axisTo = {-115.0, 12000.0, 5000.0};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 240.0;
    in.main.secHeight = 230.0;
    in.main.secWebThickness = 7.5;
    in.main.secFlangeThickness = 12.0;
    in.main.sectionCenterOffset = -115.0;
    in.main.profileRotation = 90.0;
    MemberGeometry beam;
    beam.id = 47;
    beam.axisFrom = {0.0, 12000.0, 5000.0};
    beam.axisTo = {5997.780613671378, 12000.0, 5986.683682028266};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 180.0;
    beam.secHeight = 400.0;
    beam.secWebThickness = 8.6;
    beam.secFlangeThickness = 13.5;
    beam.sectionCenterOffset = 0.0;
    beam.profileRotation = 0.0;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputManualEndPlateHeight() {
    ComponentInput in = makeInput(0.0);
    in.secondary[0].axisFrom = {0, 1200, 3000};
    in.secondary[0].axisTo = {0, 60, 3000};
    in.params = {CompParam{L"endPlateHeight", 600.0}};
    return in;
}

// --- (B) GEOMETRIK esdegerlik (digest_identity_proof_test ile ayni) ----------

static void verifyGeometricSame(const GenerationPlan& a, const GenerationPlan& b,
                                const char* label) {
    const std::string tag = label;
    CHECK(a.plates.size() == b.plates.size(), (tag + ": levha sayisi").c_str());
    const std::size_t np = std::min(a.plates.size(), b.plates.size());
    for (std::size_t i = 0; i < np; ++i) {
        const PlateSpec& x = a.plates[i];
        const PlateSpec& y = b.plates[i];
        const std::string t = tag + " plate[" + std::to_string(i) + "]";
        CHECK(nearV(x.center, y.center), (t + ": center").c_str());
        CHECK(nearV(x.u, y.u), (t + ": u").c_str());
        CHECK(nearV(x.v, y.v), (t + ": v").c_str());
        CHECK(nearV(x.axis, y.axis), (t + ": axis").c_str());
        CHECK(near(x.width, y.width), (t + ": width").c_str());
        CHECK(near(x.height, y.height), (t + ": height").c_str());
        CHECK(near(x.thickness, y.thickness), (t + ": thickness").c_str());
        CHECK(x.name == y.name, (t + ": name").c_str());
        CHECK(x.outline.size() == y.outline.size(), (t + ": outline boyutu").c_str());
        for (std::size_t k = 0; k < x.outline.size() && k < y.outline.size(); ++k) {
            CHECK(near(x.outline[k].first, y.outline[k].first) &&
                      near(x.outline[k].second, y.outline[k].second),
                  (t + ": outline noktasi").c_str());
        }
    }

    CHECK(a.boltGroups.size() == b.boltGroups.size(), (tag + ": bulon grubu sayisi").c_str());
    const std::size_t nb = std::min(a.boltGroups.size(), b.boltGroups.size());
    for (std::size_t i = 0; i < nb; ++i) {
        const BoltGroup& x = a.boltGroups[i];
        const BoltGroup& y = b.boltGroups[i];
        const std::string t = tag + " bolt[" + std::to_string(i) + "]";
        CHECK(nearV(x.origin, y.origin), (t + ": origin").c_str());
        CHECK(nearV(x.axis, y.axis), (t + ": axis").c_str());
        CHECK(nearV(x.xDir, y.xDir), (t + ": xDir").c_str());
        CHECK(x.countX == y.countX && x.countY == y.countY, (t + ": count").c_str());
        CHECK(near(x.spacingX, y.spacingX) && near(x.spacingY, y.spacingY),
              (t + ": spacing").c_str());
        CHECK(near(x.params.nominalDiameter, y.params.nominalDiameter),
              (t + ": nominalDiameter").c_str());
        CHECK(near(x.params.holeDiameter, y.params.holeDiameter),
              (t + ": holeDiameter").c_str());
        CHECK(x.params.grade == y.params.grade, (t + ": grade").c_str());
        CHECK(near(x.gripLength, y.gripLength), (t + ": gripLength").c_str());
        CHECK(near(x.holeDepth, y.holeDepth), (t + ": holeDepth").c_str());
        CHECK(x.targetOwnedPlateRole == y.targetOwnedPlateRole,
              (t + ": targetOwnedPlateRole").c_str());
    }

    CHECK(a.cuts.size() == b.cuts.size(), (tag + ": kesim sayisi").c_str());
    const std::size_t nc = std::min(a.cuts.size(), b.cuts.size());
    for (std::size_t i = 0; i < nc; ++i) {
        const CutModifier& x = a.cuts[i];
        const CutModifier& y = b.cuts[i];
        const std::string t = tag + " cut[" + std::to_string(i) + "]";
        CHECK(x.kind == y.kind, (t + ": kind").c_str());
        CHECK(nearV(x.planeOrigin, y.planeOrigin), (t + ": planeOrigin").c_str());
        CHECK(nearV(x.planeNormal, y.planeNormal), (t + ": planeNormal").c_str());
        CHECK(x.keepPositive == y.keepPositive, (t + ": keepPositive").c_str());
    }

    CHECK(a.welds.size() == b.welds.size(), (tag + ": kaynak sayisi").c_str());
    const std::size_t nw = std::min(a.welds.size(), b.welds.size());
    for (std::size_t i = 0; i < nw; ++i) {
        const WeldSpec& x = a.welds[i];
        const WeldSpec& y = b.welds[i];
        const std::string t = tag + " weld[" + std::to_string(i) + "]";
        CHECK(near(x.size, y.size), (t + ": size").c_str());
        CHECK(x.doubleSided == y.doubleSided, (t + ": doubleSided").c_str());
        CHECK(x.partARole == y.partARole, (t + ": partARole").c_str());
        CHECK(x.partBRole == y.partBRole, (t + ": partBRole").c_str());
        CHECK(x.name == y.name, (t + ": name").c_str());
    }
}

// --- (A) KANONIK: memberGeometryFromProfile ayni fiziksel uye ---------------

static bool sameGeometry(const MemberGeometry& a, const MemberGeometry& b) {
    return nearV(a.axisFrom, b.axisFrom) && nearV(a.axisTo, b.axisTo) &&
           nearV(a.refNormal, b.refNormal) &&
           near(a.secWidth, b.secWidth) && near(a.secHeight, b.secHeight) &&
           near(a.secWebThickness, b.secWebThickness) &&
           near(a.secFlangeThickness, b.secFlangeThickness) &&
           near(a.sectionCenterOffset, b.sectionCenterOffset) &&
           near(a.profileRotation, b.profileRotation);
}

static void testCanonicalRoll() {
    const Vec3 a{0.0, 0.0, 0.0}, b{6000.0, 0.0, 0.0};
    const Vec3 n{0.0, 0.0, 1.0};

    // Cift-simetrik I-kiris (centerOffset = 0): 180° roll ayni uyedir.
    const MemberSectionSpec sym{180.0, 400.0, 8.6, 13.5, 0.0};
    const MemberGeometry r0 =
        memberGeometryFromProfile(a, b, true, n, 0.0, sym);
    const MemberGeometry r180 =
        memberGeometryFromProfile(a, b, true, n, 180.0, sym);
    CHECK(sameGeometry(r0, r180), "simetrik: roll 180 == roll 0 (aynı MemberGeometry)");
    CHECK(std::fabs(r180.profileRotation) < 1e-12, "simetrik: 180° -> 0°");
    const MemberGeometry r270 =
        memberGeometryFromProfile(a, b, true, n, 270.0, sym);
    CHECK(std::fabs(r270.profileRotation - 90.0) < 1e-12, "simetrik: 270° -> 90°");
    const MemberGeometry rNeg90 =
        memberGeometryFromProfile(a, b, true, n, -90.0, sym);
    CHECK(std::fabs(rNeg90.profileRotation - 90.0) < 1e-12, "simetrik: -90° -> 90°");

    // Asimetrik kesit (centerOffset != 0, or. Top ankraj): 180° ayna goruntusu.
    const MemberSectionSpec asym{180.0, 400.0, 8.6, 13.5, -200.0};
    const MemberGeometry a0 =
        memberGeometryFromProfile(a, b, true, n, 0.0, asym);
    const MemberGeometry a180 =
        memberGeometryFromProfile(a, b, true, n, 180.0, asym);
    CHECK(std::fabs(a180.profileRotation - 180.0) < 1e-12,
          "asimetrik: 180° korunur (ayna goruntusu)");
    const MemberGeometry aNeg90 =
        memberGeometryFromProfile(a, b, true, n, -90.0, asym);
    CHECK(std::fabs(aNeg90.profileRotation - 270.0) < 1e-12,
          "asimetrik: -90° -> 270°");
    CHECK(!sameGeometry(a0, a180), "asimetrik: 0° ve 180° farkli uye");
}

// --- main -------------------------------------------------------------------

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    std::printf("=== A. KANONIK roll (memberGeometryFromProfile) ===\n");
    testCanonicalRoll();

    std::printf("=== B. GENERATOR: roll-180 == roll-0 (Haunch 40) ===\n");
    struct Case { const char* name; ComponentInput in; };
    const Case cases[5] = {
        {"haunch40_horizontal", makeInput(0.0)},
        {"haunch40_inclined_5deg", makeInput(5.0)},
        {"haunch40_inclined_10deg", makeInput(10.0)},
        {"haunch40_rotated_column_90", makeInputRotatedColumn()},
        {"haunch40_manual_endplate_height", makeInputManualEndPlateHeight()},
    };
    for (const auto& c : cases) {
        ComponentInput rolled = c.in;
        rolled.secondary[0].profileRotation =
            std::fmod(c.in.secondary[0].profileRotation + 180.0, 360.0);
        const GenerationPlan base = generateHaunch40(c.in);
        const GenerationPlan r = generateHaunch40(rolled);
        CHECK(base.ok, (std::string(c.name) + ": base ok").c_str());
        CHECK(r.ok, (std::string(c.name) + ": roll-180 ok").c_str());
        verifyGeometricSame(base, r, c.name);
        if (base.ok && r.ok) {
            const std::string d0 = generationPlanDigest(base);
            const std::string d1 = generationPlanDigest(r);
            CHECK(d0 == d1, (std::string(c.name) + ": digest roll-180 == roll-0").c_str());
            if (d0 != d1)
                std::printf("  d0=%s\n  d1=%s\n", d0.c_str(), d1.c_str());
        }
        std::printf("  %-30s plates=%zu bolts=%zu cuts=%zu welds=%zu\n",
                    c.name, base.plates.size(), base.boltGroups.size(),
                    base.cuts.size(), base.welds.size());
    }

    std::printf("haunch40 simetrik roll: %s (%d hata)\n",
                failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
