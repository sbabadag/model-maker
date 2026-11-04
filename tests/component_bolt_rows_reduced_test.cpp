// ============================================================================
// END PLATE — bolt_rows_reduced UYARI + ACIK satir fail-closed (MM-7 duzeltme).
//
// (1) VARSAYILAN (boltCountY=0=otomatik) yerlesim kucuk kesitte satir sayisini
//     2 -> 1 azalttiginda checks[]'e bilgi/uyari kaydi (bolt_rows_reduced,
//     pass=true, warning=true) ekler; plan OK kalir, bulon satiri 1'dir.
// (2) Kullanici ACIK boltCountY=2 verdiyse azaltma YAPILMAZ -> fail-closed ret
//     (aciklayici Turkce mesaj). Digest'e girmez (checks[] cikarilir).
//
// OCC gerektirmez (generateEndPlate + componentInputFromDocument + checks).
// ============================================================================
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

static const ComponentCheck* findCheck(const GenerationPlan& p, const std::string& id) {
    for (const auto& c : p.checks)
        if (c.id == id) return &c;
    return nullptr;
}

static bool setup(Document& doc, std::vector<SteelProfile>& cat, EntityId& col, EntityId& beam) {
    col = addMember(doc, "HEA300", 0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0,
                    SectionAnchor::Center);
    beam = addMember(doc, "IPE100", 0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0,
                     90.0, SectionAnchor::Center);
    return true;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    std::vector<SteelProfile> cat = loadProfileCatalog(MM_PROFILES_DIR);

    // --- (1) varsayilan (otomatik) yerlesim: satir azalir + UYARI, plan OK ----
    {
        Document doc;
        EntityId col, beam;
        setup(doc, cat, col, beam);
        auto in = componentInputFromDocument(doc, cat, col, {beam});
        CHECK(in.has_value(), "(1) girdi kuruldu");
        in->params.push_back(CompParam{L"boltDiameter", CompParamValue{20.0}});  // M20
        const GenerationPlan plan = generateEndPlate(*in);
        CHECK(plan.ok, "(1) varsayilan yerlesim OK (satir azaltildi)");
        CHECK(!plan.boltGroups.empty() && plan.boltGroups[0].countY == 1,
              "(1) bulon satiri 1'e indirildi");
        const ComponentCheck* c = findCheck(plan, "bolt_rows_reduced");
        CHECK(c != nullptr, "(1) bolt_rows_reduced UYARI check'i uretildi");
        if (c) {
            CHECK(c->pass && c->warning, "(1) uyari: pass=true + warning=true (OK acik)");
            CHECK(c->limit == 2.0 && c->value == 1.0, "(1) 2 -> 1 (deger=1, limit=2)");
        }
    }

    // --- (2) ACIK boltCountY=2: azaltma YOK -> fail-closed --------------------
    {
        Document doc;
        EntityId col, beam;
        setup(doc, cat, col, beam);
        auto in = componentInputFromDocument(doc, cat, col, {beam});
        CHECK(in.has_value(), "(2) girdi kuruldu");
        in->params.push_back(CompParam{L"boltDiameter", CompParamValue{20.0}});
        in->params.push_back(CompParam{L"boltCountY", CompParamValue{2}});  // ACIK 2 satir
        const GenerationPlan plan = generateEndPlate(*in);
        CHECK(!plan.ok, "(2) acik 2 satir fail-closed");
        CHECK(plan.error.find(L"sigmiyor") != std::wstring::npos,
              "(2) acik ret mesaji aciklayici (sigmiyor)");
    }

    std::printf("bolt rows reduced test: %s (%d hata)\n", failures ? "BASARISIZ" : "OK",
                failures);
    return failures ? 1 : 0;
}
