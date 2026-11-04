// ============================================================================
// BAGLANTI UCU GEOMETRIDEN (E / secenek C) — cizim yonu bagimsizligi testi.
//
// Ayni KIRIS iki cizim yonuyle (axisFrom/axisTo yer degistirmis) Document'e
// kondugunda componentInputFromDocument AYNI MemberGeometry'yi (id haric) ve
// AYNI planDigest'i uretmeli. End Plate rot 0/90 ve Haunch 40 icin dogrulanir.
//
// Ayrıca BELIRSIZ durum (kiris kolonun tam ortasindan geciyor, iki uc esit
// uzaklikta) ACIK ret olmali: nullopt + hata mesaji (tahmin YOK).
// ============================================================================
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static std::vector<SteelProfile> catalog() {
    std::vector<SteelProfile> cat = loadProfileCatalog(MM_PROFILES_DIR);
    SteelProfile p;
    p.name = "TESTPROFIL-400x200"; p.height = 400; p.width = 200;
    p.plateThickness = 8.6; p.flangeThickness = 13.5; cat.push_back(p);
    return cat;
}

static EntityId addMember(Document& doc, const char* name,
                          double fx, double fy, double fz,
                          double tx, double ty, double tz,
                          double nx, double ny, double nz,
                          double rotation, SectionAnchor anchor) {
    WireframeModel m = WireframeModel::line({fx, fy, fz}, {tx, ty, tz});
    EntityProperties p = m.properties();
    p.profileName = name;
    p.axisFromX = fx; p.axisFromY = fy; p.axisFromZ = fz;
    p.axisToX = tx; p.axisToY = ty; p.axisToZ = tz;
    p.sectionNormalX = nx; p.sectionNormalY = ny; p.sectionNormalZ = nz;
    p.profileRotation = rotation;
    p.sectionAnchor = anchor;
    m.setProperties(std::move(p));
    doc.addModel(std::move(m));
    return doc.modelId(doc.models().size() - 1);
}

// id DISINDA birebir ayni MemberGeometry mi?
static bool sameGeom(const MemberGeometry& a, const MemberGeometry& b) {
    const auto v = [](const Vec3& x, const Vec3& y) {
        return std::fabs(x.x - y.x) <= 1e-9 && std::fabs(x.y - y.y) <= 1e-9 &&
               std::fabs(x.z - y.z) <= 1e-9;
    };
    return v(a.axisFrom, b.axisFrom) && v(a.axisTo, b.axisTo) &&
           v(a.refNormal, b.refNormal) &&
           std::fabs(a.secWidth - b.secWidth) <= 1e-9 &&
           std::fabs(a.secHeight - b.secHeight) <= 1e-9 &&
           std::fabs(a.secWebThickness - b.secWebThickness) <= 1e-9 &&
           std::fabs(a.secFlangeThickness - b.secFlangeThickness) <= 1e-9 &&
           std::fabs(a.sectionCenterOffset - b.sectionCenterOffset) <= 1e-9 &&
           std::fabs(a.profileRotation - b.profileRotation) <= 1e-9;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto cat = catalog();

    // --- END PLATE rot 0: ayni kiris iki cizim yonuyle ---
    {
        const auto build = [&](bool flip) {
            Document doc;
            const EntityId col = addMember(doc, "HEB300",
                0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
            const EntityId beam = flip
                ? addMember(doc, "IPE400",
                    2000.0, 0.0, 3000.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center)
                : addMember(doc, "IPE400",
                    0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
            auto in = componentInputFromDocument(doc, cat, col, {beam});
            CHECK(in.has_value(), "ep rot0: girdi kuruldu");
            return in;
        };
        auto a = build(false);
        auto b = build(true);
        if (a && b) {
            CHECK(sameGeom(a->secondary[0], b->secondary[0]),
                  "ep rot0: ayni MemberGeometry (id haric)");
            const GenerationPlan pa = generateEndPlate(*a);
            const GenerationPlan pb = generateEndPlate(*b);
            CHECK(pa.ok && pb.ok, "ep rot0: planlar ok");
            if (pa.ok && pb.ok) {
                const std::string da = generationPlanDigest(pa);
                const std::string db = generationPlanDigest(pb);
                CHECK(da == db, "ep rot0: ayni digest");
                if (da != db) std::printf("  rot0: %s != %s\n", da.c_str(), db.c_str());
            }
        }
    }

    // --- END PLATE rot 90: ayni kiris iki cizim yonuyle ---
    {
        const auto build = [&](bool flip) {
            Document doc;
            const EntityId col = addMember(doc, "HEB300",
                0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
            const EntityId beam = flip
                ? addMember(doc, "TESTPROFIL-400x200",
                    2000.0, 0.0, 3000.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 90.0, SectionAnchor::Center)
                : addMember(doc, "TESTPROFIL-400x200",
                    0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 90.0, SectionAnchor::Center);
            auto in = componentInputFromDocument(doc, cat, col, {beam});
            CHECK(in.has_value(), "ep rot90: girdi kuruldu");
            return in;
        };
        auto a = build(false);
        auto b = build(true);
        if (a && b) {
            CHECK(sameGeom(a->secondary[0], b->secondary[0]),
                  "ep rot90: ayni MemberGeometry (id haric)");
            const GenerationPlan pa = generateEndPlate(*a);
            const GenerationPlan pb = generateEndPlate(*b);
            CHECK(pa.ok && pb.ok, "ep rot90: planlar ok");
            if (pa.ok && pb.ok) {
                const std::string da = generationPlanDigest(pa);
                const std::string db = generationPlanDigest(pb);
                CHECK(da == db, "ep rot90: ayni digest");
                if (da != db) std::printf("  rot90: %s != %s\n", da.c_str(), db.c_str());
            }
        }
    }

    // --- HAUNCH 40: ayni egik kiris iki cizim yonuyle ---
    {
        const auto build = [&](bool flip) {
            Document doc;
            const EntityId col = addMember(doc, "HEA240",
                -115.0, 12000.0, 0.0, -115.0, 12000.0, 5000.0,
                0.0, 0.0, 1.0, 90.0, SectionAnchor::Top);
            const EntityId beam = flip
                ? addMember(doc, "IPE400",
                    5997.780613671378, 12000.0, 5986.683682028266, 0.0, 12000.0, 5000.0,
                    0.0, 0.0, 1.0, 0.0, SectionAnchor::Center)
                : addMember(doc, "IPE400",
                    0.0, 12000.0, 5000.0, 5997.780613671378, 12000.0, 5986.683682028266,
                    0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
            auto in = componentInputFromDocument(doc, cat, col, {beam});
            CHECK(in.has_value(), "haunch: girdi kuruldu");
            return in;
        };
        auto a = build(false);
        auto b = build(true);
        if (a && b) {
            CHECK(sameGeom(a->secondary[0], b->secondary[0]),
                  "haunch: ayni MemberGeometry (id haric)");
            const GenerationPlan pa = generateHaunch40(*a);
            const GenerationPlan pb = generateHaunch40(*b);
            CHECK(pa.ok && pb.ok, "haunch: planlar ok");
            if (pa.ok && pb.ok) {
                const std::string da = generationPlanDigest(pa);
                const std::string db = generationPlanDigest(pb);
                CHECK(da == db, "haunch: ayni digest");
                if (da != db) std::printf("  haunch: %s != %s\n", da.c_str(), db.c_str());
            }
        }
    }

    // --- BELIRSIZ: kiris kolonun tam ortasindan geciyor (iki uc esit uzaklik) ---
    {
        Document doc;
        const EntityId col = addMember(doc, "HEB300",
            0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        // Kolon ekseninden -2000..+2000: iki uc da kolon eksenine esit (2000 mm).
        const EntityId beam = addMember(doc, "IPE400",
            -2000.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        std::wstring err;
        auto in = componentInputFromDocument(doc, cat, col, {beam}, {}, &err);
        CHECK(!in.has_value(), "belirsiz: girdi REDDEDILDI (nullopt)");
        CHECK(!err.empty(), "belirsiz: hata mesaji dolu");
        if (!err.empty()) std::printf("  ret: %ls\n", err.c_str());
    }

    std::printf("baglanti ucu geometriden testi: %s (%d hata)\n",
                failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
