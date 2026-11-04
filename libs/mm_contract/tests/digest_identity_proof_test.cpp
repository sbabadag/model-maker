// ============================================================================
// MM-2b KANIT TESTI — digest kimlik-bagimsizligina gecerken GEOMETRI DEGISMEDI.
//
// Golden dosyalari yeniden uretilmeden ONCE bu test YESIL olmalidir. Test,
// golden dosyalarindan BAGIMSIZDIR (hic okumaz); yalniz su iki seyi kanitlar:
//
//   (1) GEOMETRIK esdegerlik: her golden girdi icin generator plani ile recipe
//       plani (paket recipe.json'dan) GEOMETRIK olarak ozdestir — levha
//       cercevesi (center/u/v/axis/width/height/thickness/outline = koseler),
//       bulon eksenleri/grip (origin/axis/xDir/count/spacing/delik/grip),
//       kesim duzlemleri (kind/planeOrigin/planeNormal/keepPositive) ve kaynak
//       uc rolleri (size/doubleSided/partARole/partBRole) — 1e-9 toleransla.
//
//   (2) TEK FARK id -> rol donusumu: plandaki rol alanlari (BoltGroup.partRoles,
//       CutModifier.targetRole, WeldSpec.partARole/partBRole) EntityId alanlarinin
//       BIREBIR yeniden etiketlenmesidir. `inputRoleFor(in, parts[i]) ==
//       partRoles[i]` gibi — rol, ayni uyeyi gosterir, geometriyi degistirmez.
//
// Cevap anahtari: inputRoleFor (component_engine.hpp) EntityId -> rol eslemesini
// yapar; bu test tersini (rol -> EntityId) cozup parts/target/partA ile karsilastirir.
// ============================================================================
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/haunch40.hpp"
#include "model_maker/recipe.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kEps = 1e-9;

#ifndef MM_PACKAGES_DIR
#error "MM_PACKAGES_DIR tanimli olmali"
#endif

// --- yardimcilar ------------------------------------------------------------

static bool near(double a, double b) { return std::fabs(a - b) <= kEps; }

static bool nearV(const Vec3& a, const Vec3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

static std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Rol anahtari -> EntityId (inputRoleFor'un tersi). Uye rolu degilse invalid.
static EntityId resolveRole(const ComponentInput& in, const std::string& role) {
    if (role == "main") return in.main.id;
    const std::string prefix = "secondary_";
    if (role.rfind(prefix, 0) == 0) {
        const std::string idx = role.substr(prefix.size());
        if (!idx.empty() && idx.find_first_not_of("0123456789") == std::string::npos) {
            const unsigned long n = std::stoul(idx);
            if (n < in.secondary.size()) return in.secondary[n].id;
        }
    }
    return kInvalidEntityId;
}

// (2) id -> rol donusumunun SADAKATI: rol alanlari EntityId alanlarini birebir
//     yeniden etiketler. Bu, digest'in rol kullanmasinin geometriyi degistirmedigini
//     kanitlar.
static void verifyRoleFaithful(const GenerationPlan& p, const ComponentInput& in,
                               const char* label) {
    for (std::size_t i = 0; i < p.boltGroups.size(); ++i) {
        const BoltGroup& b = p.boltGroups[i];
        const std::string tag = std::string(label) + " bolt[" + std::to_string(i) + "]";
        CHECK(b.partRoles.size() == b.parts.size(),
              (tag + ": partRoles ile parts ayni boyutta").c_str());
        for (std::size_t k = 0; k < b.parts.size() && k < b.partRoles.size(); ++k) {
            const EntityId resolved = resolveRole(in, b.partRoles[k]);
            CHECK(resolved == b.parts[k],
                  (tag + ": partRoles[k] -> parts[k] (id=rol birebir)").c_str());
        }
    }
    for (std::size_t i = 0; i < p.cuts.size(); ++i) {
        const CutModifier& c = p.cuts[i];
        const std::string tag = std::string(label) + " cut[" + std::to_string(i) + "]";
        CHECK(resolveRole(in, c.targetRole) == c.target,
              (tag + ": targetRole -> target").c_str());
    }
    for (std::size_t i = 0; i < p.welds.size(); ++i) {
        const WeldSpec& w = p.welds[i];
        const std::string tag = std::string(label) + " weld[" + std::to_string(i) + "]";
        const EntityId ra = resolveRole(in, w.partARole);
        if (ra != kInvalidEntityId) {
            CHECK(ra == w.partA, (tag + ": partARole (uye) -> partA").c_str());
        } else {
            CHECK(w.partA == kInvalidEntityId,
                  (tag + ": partARole levha rolu -> partA kimliksiz").c_str());
        }
        const EntityId rb = resolveRole(in, w.partBRole);
        if (rb != kInvalidEntityId) {
            CHECK(rb == w.partB, (tag + ": partBRole (uye) -> partB").c_str());
        } else {
            CHECK(w.partB == kInvalidEntityId,
                  (tag + ": partBRole levha rolu -> partB kimliksiz").c_str());
        }
    }
}

// (1) GEOMETRIK esdegerlik: iki plan ayni geometriyi tarif etmeli (1e-9).
//     Kimlik alanlari (EntityId, bolt id, display, shapeParts) KARSILASTIRILMAZ.
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

// --- HAUNCH 40 golden girdileri (haunch40_equivalence_test ile ayni) --------

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

// --- END PLATE golden girdileri (end_plate_equivalence_test ile ayni) -------

static ComponentInput makeCase(int kind) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = Vec3{0.0, 0.0, 0.0};
    in.main.axisTo = Vec3{0.0, 0.0, 3000.0};
    in.main.refNormal = Vec3{0.0, 0.0, 1.0};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;

    MemberGeometry b;
    b.id = 2;
    b.refNormal = Vec3{0.0, 0.0, 1.0};
    b.secWidth = 200.0;
    b.secHeight = 400.0;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;

    switch (kind) {
        case 0:
            b.axisFrom = Vec3{0.0, 0.0, 3000.0};
            b.axisTo = Vec3{2000.0, 0.0, 3000.0};
            break;
        case 1:
            b.axisFrom = Vec3{0.0, 0.0, 3000.0};
            b.axisTo = Vec3{-2000.0, 0.0, 3000.0};
            break;
        case 2:
            b.axisFrom = Vec3{0.0, 0.0, 3000.0};
            b.axisTo = Vec3{0.0, 2000.0, 3000.0};
            break;
        case 3:
            b.axisFrom = Vec3{0.0, 0.0, 3000.0};
            b.axisTo = Vec3{2000.0, 0.0, 3000.0};
            b.profileRotation = 90.0;
            break;
        case 4:
            b.axisFrom = Vec3{0.0, 0.0, 3000.0};
            b.axisTo = Vec3{2000.0, 0.0, 3000.0};
            in.params.push_back(CompParam{L"boltCountX", CompParamValue{2}});
            in.params.push_back(CompParam{L"boltCountY", CompParamValue{2}});
            in.params.push_back(CompParam{L"boltDiameter", CompParamValue{24.0}});
            in.params.push_back(CompParam{L"holeDiameter", CompParamValue{26.0}});
            in.params.push_back(CompParam{L"boltEdge", CompParamValue{40.0}});
            break;
        default:
            break;
    }
    in.secondary.push_back(b);
    return in;
}

// --- main -------------------------------------------------------------------

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    const std::string haunchRecipeText =
        readText(std::string(MM_PACKAGES_DIR) + "/haunch40/recipe.json");
    const std::string endPlateRecipeText =
        readText(std::string(MM_PACKAGES_DIR) + "/end_plate/recipe.json");
    CHECK(!haunchRecipeText.empty(), "packages/haunch40/recipe.json okundu");
    CHECK(!endPlateRecipeText.empty(), "packages/end_plate/recipe.json okundu");
    if (haunchRecipeText.empty() || endPlateRecipeText.empty()) {
        std::printf("MM-2b kanit testi: %s (%d hata)\n", failures ? "BASARISIZ" : "OK", failures);
        return failures ? 1 : 0;
    }

    const Recipe haunchRecipe = recipeFromJson(nlohmann::json::parse(haunchRecipeText, nullptr, false));
    const Recipe endPlateRecipe = recipeFromJson(nlohmann::json::parse(endPlateRecipeText, nullptr, false));
    const std::vector<CompParamSpec> haunchSpecs = haunch40ComponentType().params;
    const std::vector<CompParamSpec> endPlateSpecs = endPlateComponentType().params;

    std::printf("=== HAUNCH 40 (5 golden girdi) ===\n");
    {
        struct Case { const char* name; ComponentInput in; };
        const Case cases[5] = {
            {"haunch40_horizontal", makeInput(0.0)},
            {"haunch40_inclined_5deg", makeInput(5.0)},
            {"haunch40_inclined_10deg", makeInput(10.0)},
            {"haunch40_rotated_column_90", makeInputRotatedColumn()},
            {"haunch40_manual_endplate_height", makeInputManualEndPlateHeight()},
        };
        for (const auto& c : cases) {
            const GenerationPlan gen = generateHaunch40(c.in);
            const GenerationPlan rec = runRecipe(haunchRecipe, haunchSpecs, c.in);
            CHECK(gen.ok, (std::string(c.name) + ": generateHaunch40 ok").c_str());
            CHECK(rec.ok, (std::string(c.name) + ": runRecipe ok").c_str());
            verifyGeometricSame(gen, rec, c.name);
            verifyRoleFaithful(gen, c.in, c.name);
            verifyRoleFaithful(rec, c.in, c.name);
            std::printf("  %-30s plates=%zu bolts=%zu cuts=%zu welds=%zu\n",
                        c.name, gen.plates.size(), gen.boltGroups.size(),
                        gen.cuts.size(), gen.welds.size());
        }
    }

    std::printf("=== END PLATE (5 golden girdi) ===\n");
    {
        const char* names[5] = {"endplate_beam_px", "endplate_beam_nx", "endplate_beam_py",
                                "endplate_rotated_90", "endplate_diff_bolts"};
        for (int k = 0; k < 5; ++k) {
            const ComponentInput in = makeCase(k);
            const GenerationPlan gen = generateEndPlate(in);
            const GenerationPlan rec = runRecipe(endPlateRecipe, endPlateSpecs, in);
            CHECK(gen.ok, (std::string(names[k]) + ": generateEndPlate ok").c_str());
            CHECK(rec.ok, (std::string(names[k]) + ": runRecipe ok").c_str());
            verifyGeometricSame(gen, rec, names[k]);
            verifyRoleFaithful(gen, in, names[k]);
            verifyRoleFaithful(rec, in, names[k]);
            std::printf("  %-20s plates=%zu bolts=%zu cuts=%zu welds=%zu\n",
                        names[k], gen.plates.size(), gen.boltGroups.size(),
                        gen.cuts.size(), gen.welds.size());
        }
    }

    std::printf("MM-2b kanit testi: %s (%d hata)\n", failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
