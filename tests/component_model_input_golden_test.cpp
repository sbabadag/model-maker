// ============================================================================
// MODELDEN KURULAN GIRDI ALTIN TESTI — Document -> componentInputFromDocument
// -> generator -> digest (golden'a karsi).
//
// NEDEN (42f61ee regresyonu): altin testler MemberGeometry'yi C++'ta ELDE
// kuruyordu; GERCEK GUI yolu (application.cpp memberGeometryFor -> kanonik
// memberGeometryFromProfile) altin testlerden GECMIYORDU. d8e10b6 kanonik
// fonksiyonu dusey kolonda (n ∥ axis) kesit cercevesini 90° cevirince model
// bozuldu ama 163 test yesil kaldi. Bu test, Document'ten kurulan girdiyi
// generatora verir ve ayni fiziksel senaryonun elde-kurulan golden'iyle
// BIREBIR ayni digest'i urettigini dogrular (kör nokta kapandi).
//
// Id normalizasyonu YOK (MM-2b): digest EntityId'den bagimsizdir; Document
// id'leri insersiyon sirasina gore atansa da (haunch 45/47, end plate 1/2)
// digest ayni kalir. Asagidaki kimlik-bagimsizlik blogu bunu ayrica dogrular.
// ============================================================================
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string goldenDigest(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t p = json.find(needle);
    if (p == std::string::npos) return {};
    p = json.find(':', p);
    if (p == std::string::npos) return {};
    const std::size_t q1 = json.find('"', p);
    if (q1 == std::string::npos) return {};
    const std::size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// KATALOG: GERCEK Avrupa profilleri `profiles/european.lis`'ten yuklenir
// (item 4: golden kor noktasi — eski testler sahte 200x400 "IPE200" olcusuyla
// kosuyordu, hicbir katalogda olmayan bir kesit). ESKI 200x400 profil, eski
// golden (endplate_rotated_90) ona bagli oldugu icin digest churn'unu onlemek
// adina KORUNUR ama artik DURUST adla: "TESTPROFIL-400x200" (gercek IPE200
// 100x200 yeni katalog matrisinde ayrica test edilir).
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

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto cat = catalog();
    const std::string goldenDir = MM_CONTRACT_GOLDEN_DIR;

    // ---- HAUNCH 40: donuk kolon (n ∥ axis) + egik kiris — regresyon senaryosu.
    {
        Document doc;
        const EntityId col = addMember(doc, "HEA240",
            -115.0, 12000.0, 0.0, -115.0, 12000.0, 5000.0,
            0.0, 0.0, 1.0, 90.0, SectionAnchor::Top);
        const EntityId beam = addMember(doc, "IPE400",
            0.0, 12000.0, 5000.0, 5997.780613671378, 12000.0, 5986.683682028266,
            0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        auto in = componentInputFromDocument(doc, cat, col, {beam});
        CHECK(in.has_value(), "haunch: modelden girdi kuruldu");
        if (!in) return 1;
        const GenerationPlan plan = generateHaunch40(*in);
        CHECK(plan.ok, "haunch: generateHaunch40 ok");
        if (!plan.ok) std::printf("  error=%ls\n", plan.error.c_str());
        const std::string dg = generationPlanDigest(plan);
        const std::string dgold =
            goldenDigest(readText(goldenDir + "/haunch40_digests.json"),
                         "haunch40_rotated_column_90");
        CHECK(!dgold.empty(), "haunch: golden okundu");
        CHECK(dg == dgold, "haunch: modelden kurulan digest == golden (kör nokta kapandi)");
        if (dg != dgold)
            std::printf("  model=%s\n  golden=%s\n", dg.c_str(), dgold.c_str());
    }

    // ---- END PLATE: kiriş 90° dönük (endplate_rotated_90 senaryosu).
    {
        Document doc;
        const EntityId col = addMember(doc, "HEB300",
            0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
        const EntityId beam = addMember(doc, "TESTPROFIL-400x200",
            0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 90.0, SectionAnchor::Center);
        auto in = componentInputFromDocument(doc, cat, col, {beam});
        CHECK(in.has_value(), "endplate: modelden girdi kuruldu");
        if (!in) return 1;
        const GenerationPlan plan = generateEndPlate(*in);
        CHECK(plan.ok, "endplate: generateEndPlate ok");
        if (!plan.ok) std::printf("  error=%ls\n", plan.error.c_str());
        const std::string dg = generationPlanDigest(plan);
        const std::string dgold =
            goldenDigest(readText(goldenDir + "/end_plate_digests.json"),
                         "endplate_rotated_90");
        CHECK(!dgold.empty(), "endplate: golden okundu");
        CHECK(dg == dgold, "endplate: modelden kurulan digest == golden");
        if (dg != dgold)
            std::printf("  model=%s\n  golden=%s\n", dg.c_str(), dgold.c_str());
    }

    // ---- KIMLIK BAGIMSIZLIK (MM-2b): ayni fiziksel girdi, farkli EntityId ---
    // Ayni haunch senaryosu iki Document'te farkli insersiyon sirasiyla kurulur;
    // EntityId'ler farkli olur ama digest AYNI kalmali (digest kimlik-bagimsiz).
    {
        const auto buildDoc = [&](EntityId& col, EntityId& beam, bool beamFirst) {
            Document doc;
            if (beamFirst) {
                beam = addMember(doc, "IPE400",
                    0.0, 12000.0, 5000.0, 5997.780613671378, 12000.0, 5986.683682028266,
                    0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
                col = addMember(doc, "HEA240",
                    -115.0, 12000.0, 0.0, -115.0, 12000.0, 5000.0,
                    0.0, 0.0, 1.0, 90.0, SectionAnchor::Top);
            } else {
                col = addMember(doc, "HEA240",
                    -115.0, 12000.0, 0.0, -115.0, 12000.0, 5000.0,
                    0.0, 0.0, 1.0, 90.0, SectionAnchor::Top);
                beam = addMember(doc, "IPE400",
                    0.0, 12000.0, 5000.0, 5997.780613671378, 12000.0, 5986.683682028266,
                    0.0, 0.0, 1.0, 0.0, SectionAnchor::Center);
            }
            return doc;
        };
        EntityId c1, b1, c2, b2;
        Document d1 = buildDoc(c1, b1, false);
        Document d2 = buildDoc(c2, b2, true);
        CHECK(c1 != c2 && b1 != b2, "kimlik: EntityId'ler farkli (on kosul)");
        auto in1 = componentInputFromDocument(d1, cat, c1, {b1});
        auto in2 = componentInputFromDocument(d2, cat, c2, {b2});
        CHECK(in1.has_value() && in2.has_value(), "kimlik: girdiler kuruldu");
        if (in1 && in2) {
            const GenerationPlan p1 = generateHaunch40(*in1);
            const GenerationPlan p2 = generateHaunch40(*in2);
            CHECK(p1.ok && p2.ok, "kimlik: planlar ok");
            if (p1.ok && p2.ok) {
                const std::string dg1 = generationPlanDigest(p1);
                const std::string dg2 = generationPlanDigest(p2);
                CHECK(dg1 == dg2, "kimlik: farkli EntityId -> ayni digest");
                if (dg1 != dg2)
                    std::printf("  d1=%s\n  d2=%s\n", dg1.c_str(), dg2.c_str());
            }
        }
    }

    // ---- GERCEK KATALOG MATRISI (item 4: golden kor noktasi) --------------
    // Kolonlar {HEA300, HEB300} x kirisler {IPE200 100x200, IPE300, IPE400}
    // x bulon {M16, M20}. Tum olculer profiles/european.lis'ten GERCEK
    // katalog kesitleridir. Her vakada End Plate reddedilmemeli (checks
    // gecmeli; item 1 varsayilan gauge EC3'e uygun) ve digest golden ile
    // birebir olmali. Golden yoksa digest'ler GENDIGEST satiriyla basilir.
    {
        const char* cols[] = {"HEA300", "HEB300"};
        // MM-7 item 2: "sinirda" kesitler eklendi (IPE220/240/270, HEA240, HEB260).
        // HEB260 tf=17.5 M20/M24'te varsayilan dikey edge'i 40/45'e buyutur (baslik
        // acikligi); digerleri edge=35'te kalir. M24 ile edge-buyutme yolu kilitlenir.
        const char* beams[] = {"IPE200", "IPE300", "IPE400", "IPE220", "IPE240",
                               "IPE270", "HEA240", "HEB260"};
        const double diams[] = {16.0, 20.0, 24.0};  // M16, M20, M24
        const std::string gpath = goldenDir + "/end_plate_catalog_digests.json";
        const std::string gj = readText(gpath);
        CHECK(!gj.empty(), "katalog: golden dosyasi okundu");
        for (const char* cn : cols) {
            for (const char* bn : beams) {
                for (double dia : diams) {
                    Document doc;
                    const EntityId col = addMember(doc, cn,
                        0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0,
                        SectionAnchor::Center);
                    const EntityId beam = addMember(doc, bn,
                        0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 90.0,
                        SectionAnchor::Center);
                    auto in = componentInputFromDocument(doc, cat, col, {beam});
                    char key[96];
                    std::snprintf(key, sizeof key, "ep_%s_%s_M%d", cn, bn, (int)dia);
                    if (!in) {
                        std::printf("FAIL: %s girdi kurulamadi\n", key);
                        ++failures;
                        continue;
                    }
                    in->params.push_back(CompParam{L"boltDiameter", CompParamValue{dia}});
                    const GenerationPlan plan = generateEndPlate(*in);
                    if (!plan.ok) {
                        std::printf("FAIL: %s reddedildi (checks/geo): %ls\n", key,
                                    plan.error.c_str());
                        ++failures;
                        continue;
                    }
                    const std::string dg = generationPlanDigest(plan);
                    const std::string dgold = goldenDigest(gj, key);
                    if (dgold.empty()) {
                        std::printf("GENDIGEST %s=%s\n", key, dg.c_str());
                        ++failures;
                    } else if (dg != dgold) {
                        std::printf("FAIL: %s\n  model=%s\n  golden=%s\n",
                                    key, dg.c_str(), dgold.c_str());
                        ++failures;
                    }
                }
            }
        }
    }

    std::printf("modelden kurulan girdi altin testi: %s (%d hata)\n",
                failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
