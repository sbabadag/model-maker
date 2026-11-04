// ============================================================================
// END PLATE VARSAYILAN KAPSAM TESTI (MM-7 item 2).
//
// profiles/european.lis'teki TUM IPE/HEA/HEB kiris kesitleri x {M16, M20, M24}
// x {HEA300, HEB300 kolon} icin VARSAYILAN End Plate plani ya checks'ten gecmeli
// ya da ACIK "desteklenmiyor" (varsayilan bulon yerlesimi yok) vermeli. Beklenmeyen
// (sessiz) bir ret veya check ihlali = FAIL; gecmeyen kesitler raporlanir.
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

static bool hasPrefix(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<SteelProfile> cat = loadProfileCatalog(MM_PROFILES_DIR);

    // Katalogdan IPE/HEA/HEB kiris kesitleri (buyuk/kucuk harf duyarsiz on ek).
    std::vector<const SteelProfile*> beams;
    for (const SteelProfile& p : cat) {
        if (hasPrefix(p.name, "IPE") || hasPrefix(p.name, "HEA") || hasPrefix(p.name, "HEB"))
            beams.push_back(&p);
    }
    CHECK(!beams.empty(), "katalogda IPE/HEA/HEB kesit var");

    const char* cols[] = {"HEA300", "HEB300"};
    const double diams[] = {16.0, 20.0, 24.0};  // M16, M20, M24

    int passCount = 0, unsupportedCount = 0, total = 0;
    std::vector<std::string> unsupported;

    for (const char* cn : cols) {
        for (const SteelProfile* bp : beams) {
            for (double dia : diams) {
                ++total;
                Document doc;
                const EntityId col = addMember(doc, cn,
                    0.0, 0.0, 0.0, 0.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 0.0,
                    SectionAnchor::Center);
                const EntityId beam = addMember(doc, bp->name.c_str(),
                    0.0, 0.0, 3000.0, 2000.0, 0.0, 3000.0, 0.0, 0.0, 1.0, 90.0,
                    SectionAnchor::Center);
                auto in = componentInputFromDocument(doc, cat, col, {beam});
                if (!in) {
                    std::printf("FAIL: %s x %s M%d girdi kurulamadi\n", cn, bp->name.c_str(),
                                (int)dia);
                    ++failures;
                    continue;
                }
                in->params.push_back(CompParam{L"boltDiameter", CompParamValue{dia}});
                const GenerationPlan plan = generateEndPlate(*in);
                if (plan.ok) {
                    ++passCount;
                } else if (plan.error.find(L"yerlesimi yok") != std::wstring::npos) {
                    ++unsupportedCount;
                    unsupported.push_back(std::string(bp->name) + " M" +
                                          std::to_string((int)dia) + " [" + cn + "]");
                } else {
                    std::printf("FAIL: %s x %s M%d beklenmeyen ret: %ls\n", cn,
                                bp->name.c_str(), (int)dia, plan.error.c_str());
                    ++failures;
                }
            }
        }
    }

    std::printf("\nkapsam: %d kesit x %d cap x %d kolon = %d vaka\n",
                (int)beams.size(), (int)(sizeof(diams) / sizeof(diams[0])),
                (int)(sizeof(cols) / sizeof(cols[0])), total);
    std::printf("gecen=%d  desteklenmiyor(acik ret)=%d  beklenmeyen=%d\n", passCount,
                unsupportedCount, failures);
    if (!unsupported.empty()) {
        std::printf("desteklenmeyen kesitler:\n");
        for (const std::string& s : unsupported) std::printf("  %s\n", s.c_str());
    }

    std::printf("end plate kapsam testi: %s (%d hata)\n", failures ? "BASARISIZ" : "OK",
                failures);
    return failures ? 1 : 0;
}
