// ============================================================================
// H-2 ROL COZUMU / TERS SECIM testi — "End Plate ters rol sirasinda levhayi
// baglantidan uzaga koyuyor".
//
// Kural (fail-closed): kiriş-kolon bağlantılarında main = KOLON (dikey destek),
// secondary = KIRIS (ucu kolon yüzüne temas eden üye). Kullanıcı ters sırada
// seçebilir (önce kiriş); dikeylikle geometriden çözülmeli ve sonuç secim
// sirasindan BAĞIMSIZ olmali (levha hep kolon yüzünde, kiris ucu kesimi hep
// kiriste, bulonlar hep kolonu deler, digest ayni).
//
// GUI yolu (application.cpp applyComponentFromParts) MAIN ve SECONDARY'yi
// AYNI `memberGeometryFor` (-> memberGeometryFromEntityProperties) ile kurar;
// bu test ayni yolu birebir kullanir (ters secim = main'e kiris, secondary'ye
// kolon). Belirsiz durum (iki üye benzer dikeylik) ACIK red olmali.
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
    return loadProfileCatalog(MM_PROFILES_DIR);
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

// GUI yolu: main ve secondary AYNI memberGeometryFromEntityProperties ile kurulur.
static std::pair<ComponentInput, ComponentInput> buildInputs(Document& doc,
                                                              EntityId col,
                                                              EntityId beam) {
    const std::size_t ci = *doc.indexOfEntity(col);
    const std::size_t bi = *doc.indexOfEntity(beam);
    const auto cat = catalog();
    MemberGeometry colG = memberGeometryFromEntityProperties(doc.models()[ci].properties(), cat);
    MemberGeometry beamG = memberGeometryFromEntityProperties(doc.models()[bi].properties(), cat);
    colG.id = col;
    beamG.id = beam;
    ComponentInput correct;
    correct.main = colG;
    correct.secondary = {beamG};
    ComponentInput reversed;
    reversed.main = beamG;
    reversed.secondary = {colG};
    return {correct, reversed};
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // ---- END PLATE: kolon HEB300 + kiris IPE400 (rot 0) ----
    {
        Document doc;
        const EntityId col = addMember(doc, "HEB300",
            0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        const EntityId beam = addMember(doc, "IPE400",
            0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        auto [correct, reversed] = buildInputs(doc, col, beam);

        const ComponentRoleResolution rc = componentRolesFromGeometry(correct);
        CHECK(rc.checked && rc.determinable && !rc.swap,
              "ep: dogru secim -> swap=false");
        const ComponentRoleResolution rr = componentRolesFromGeometry(reversed);
        CHECK(rr.checked && rr.determinable && rr.swap,
              "ep: ters secim -> swap=true");

        const GenerationPlan pc = generateEndPlate(correct);
        const GenerationPlan pr = generateEndPlate(reversed);
        CHECK(pc.ok, "ep: dogru secim plani ok");
        CHECK(pr.ok, "ep: ters secim plani ok (sessiz red YOK)");
        if (pc.ok && pr.ok) {
            const std::string dc = generationPlanDigest(pc);
            const std::string dr = generationPlanDigest(pr);
            CHECK(dc == dr, "ep: ters secim digest == dogru secim digest");
            if (dc != dr)
                std::printf("  correct=%s\n  reversed=%s\n", dc.c_str(), dr.c_str());
        }
        // Bulonlar KOLONU delmeli, kiris ucu kesimi KIRISI kesmeli (ters secimde bile).
        if (pr.ok) {
            bool boltHitsCol = false;
            for (const auto& b : pr.boltGroups)
                if (!b.parts.empty() && b.parts[0] == col) boltHitsCol = true;
            CHECK(boltHitsCol, "ep ters: bulon grubu kolonu deliyor");
            bool cutHitsBeam = false, cutHitsCol = false;
            for (const auto& c : pr.cuts) {
                if (c.target == beam) cutHitsBeam = true;
                if (c.target == col) cutHitsCol = true;
            }
            CHECK(cutHitsBeam, "ep ters: kiris ucu kesimi KIRISI kesiyor (kolonu DEGIL)");
            CHECK(!cutHitsCol, "ep ters: kiris ucu kesimi kolonu kesmiyor");
        }
    }

    // ---- FIN PLATE: ayni kolon/kiris ----
    {
        Document doc;
        const EntityId col = addMember(doc, "HEB300",
            0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        const EntityId beam = addMember(doc, "IPE400",
            0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        auto [correct, reversed] = buildInputs(doc, col, beam);
        const GenerationPlan pc = generateFinPlate(correct);
        const GenerationPlan pr = generateFinPlate(reversed);
        CHECK(pc.ok && pr.ok, "fin: planlar ok");
        if (pc.ok && pr.ok) {
            const std::string dc = generationPlanDigest(pc);
            const std::string dr = generationPlanDigest(pr);
            CHECK(dc == dr, "fin: ters secim digest == dogru secim digest");
            if (dc != dr)
                std::printf("  correct=%s\n  reversed=%s\n", dc.c_str(), dr.c_str());
        }
    }

    // ---- HAUNCH 40: donuk kolon + egik mertek ----
    {
        Document doc;
        const EntityId col = addMember(doc, "HEA240",
            -115.0, 12000.0, 0.0, -115.0, 12000.0, 5000.0,
            0.0, 0.0, 1.0, 90.0, SectionAnchor::Top);
        const EntityId beam = addMember(doc, "IPE400",
            0.0, 12000.0, 5000.0, 5997.780613671378, 12000.0, 5986.683682028266,
            0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        auto [correct, reversed] = buildInputs(doc, col, beam);

        const ComponentRoleResolution rr = componentRolesFromGeometry(reversed);
        CHECK(rr.checked && rr.determinable && rr.swap,
              "haunch: egik mertek ters secim -> swap=true");

        const GenerationPlan pc = generateHaunch40(correct);
        const GenerationPlan pr = generateHaunch40(reversed);
        CHECK(pc.ok && pr.ok, "haunch: planlar ok");
        if (pc.ok && pr.ok) {
            const std::string dc = generationPlanDigest(pc);
            const std::string dr = generationPlanDigest(pr);
            CHECK(dc == dr, "haunch: ters secim digest == dogru secim digest");
            if (dc != dr)
                std::printf("  correct=%s\n  reversed=%s\n", dc.c_str(), dr.c_str());
        }
    }

    // ---- BELIRSIZ: iki yatay kiris (dikeylik ayirt edilemez) -> fail-closed red ----
    {
        Document doc;
        const EntityId a = addMember(doc, "IPE400",
            0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        const EntityId b = addMember(doc, "IPE400",
            0.0, 1000.0, 3000.0, 2000.0, 1000.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        auto [in, ignored] = buildInputs(doc, a, b);
        (void)ignored;
        const ComponentRoleResolution r = componentRolesFromGeometry(in);
        CHECK(r.checked, "belirsiz: kontrol calisti");
        CHECK(!r.determinable, "belirsiz: determinable=false (iki yatay kiris)");
        CHECK(!r.reason.empty(), "belirsiz: red mesaji dolu");
        if (!r.reason.empty()) std::printf("  ret: %ls\n", r.reason.c_str());
    }

    std::printf("H-2 rol cozumu / ters secim testi: %s (%d hata)\n",
                failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
