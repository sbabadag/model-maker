// P-2/2 — ANKRAJ BULONU UYEYE DELIK ACMAZ (obje uyumlulugu).
//
// Kontrat (docs/agent/P-1-STUDIO-PAKETLERI.md, P-2 tablosu):
//   `bolts.grid`                  -> through uyelerde VE levhalarda delik acar
//   `bolts.grid` ankraj (bos through) -> uyeye delik ACMAZ; yalniz levhada delik
//
// Bu test iki yolu GERCEK OCCT materializer + Document uzerinde kurar:
//   A) Taban levhasi paketi (`mm_stiffened_base_plate`): ankraj grubu
//      `through: []`. Plan seviyesinde `partRoles` uye icermez; commit sonrasi
//      grubun `parts` listesi YALNIZ komponentin kendi levhalaridir ve
//      `boltGroupHoleModifiers` yalniz levhalari hedefler. Kolon delinmez.
//   B) Alin levhasi paketi (`endplate.placement` -> `beam_bolts`): normal grup
//      `parts = {main}`. Commit sonrasi ana uye grupta KALIR -> uye delinir.
//
// Delik acma karari `bolt.parts` uzerinden verilir (src/bolt.cpp
// boltGroupHoleModifiers); bu yuzden "uyeye delik acilmaz" iddiasi tam olarak
// "ana uye grubun parts'inda YOK" demektir ve asagida oyle denetlenir.

#include "model_maker/component_codec.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/recipe.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

// Kanonik girdi: HEB300 kolon (0,0,0)-(0,0,3000). Taban levhasi icin yalniz
// kolon; alin levhasi icin +IPE240 kiris.
static ComponentInput makeInput(bool withBeam) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0.0, 0.0, 0.0};
    in.main.axisTo = {0.0, 0.0, 3000.0};
    in.main.refNormal = {1.0, 0.0, 0.0};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;
    if (withBeam) {
        MemberGeometry b;
        b.id = 2;
        b.axisFrom = {0.0, 120.0, 2880.0};
        b.axisTo = {6000.0, 120.0, 2880.0};
        b.refNormal = {0.0, 0.0, 1.0};
        b.secWidth = 120.0;
        b.secHeight = 240.0;
        b.secWebThickness = 6.2;
        b.secFlangeThickness = 9.8;
        in.secondary.push_back(b);
    }
    return in;
}

// Belgeye gercek B-Rep'li uyeler koyar (delik denetimi icin sart: delik
// modifier'lari hedef katilar uzerinde uygulanir).
static void seedMembers(Document& doc, OccSolidStore* store, bool withBeam) {
    WireframeModel col = WireframeModel::cube(100.0);
    doc.addModel(std::move(col));
    if (withBeam) {
        WireframeModel beam = WireframeModel::cube(100.0);
        doc.addModel(std::move(beam));
    }
    if (!store) return;
    for (std::size_t i = 0; i < doc.models().size(); ++i) {
        PlateSpec box;
        box.center = {0.0, 0.0, i == 0 ? 0.0 : 2880.0};
        box.u = {1.0, 0.0, 0.0};
        box.v = {0.0, 1.0, 0.0};
        box.axis = {0.0, 0.0, 1.0};
        box.width = 300.0;
        box.height = 300.0;
        box.thickness = 2000.0;
        const TopoDS_Shape solid = ComponentMaterializer::buildPlateSolid(box);
        if (solid.IsNull()) continue;
        const EntityId id = doc.modelId(i);
        store->set(id, solid);
        store->setBaseVersion(id, store->activeVersion(id));
    }
}

static bool contains(const std::vector<EntityId>& v, EntityId id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}
static bool hasRole(const std::vector<std::string>& v, const char* r) {
    return std::find(v.begin(), v.end(), std::string(r)) != v.end();
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::filesystem::path pkgs = std::filesystem::path(MM_FIXTURES_DIR) / "studio_pkgs";

    // --- A) ANKRAJ: taban levhasi paketi (through: []) -----------------------
    {
        ComponentRegistry scratch;
        registerBuiltInComponents(scratch);
        const ComponentPackageLoad pkg =
            loadComponentPackage(pkgs / "mm_stiffened_base_plate.mmcpkg", scratch);
        check(pkg.ok, "A taban levhasi paketi yuklendi");
        if (!pkg.ok && !pkg.message.empty())
            std::printf("      [teshis] %ls\n", pkg.message.c_str());
        if (pkg.ok) {
            const ComponentInput in = makeInput(/*withBeam=*/false);
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, in);
            check(plan.ok, "A plan uretildi");
            if (plan.ok) {
                check(!plan.boltGroups.empty(), "A plan en az bir bulon grubu uretti");
                bool anchorsOnly = true;
                int boltCount = 0;
                for (const auto& g : plan.boltGroups) {
                    if (hasRole(g.partRoles, "main") || hasRole(g.partRoles, "secondary_0"))
                        anchorsOnly = false;
                    boltCount += g.countX * g.countY;
                }
                check(anchorsOnly, "A plan: ankraj grubunda UYE rolu yok (through bos)");
                check(boltCount >= 4, "A plan: ankraj sayisi >= 4");

                Document doc;
                doc.setOccStore(makeOccSolidStore());
                OccSolidStore* store = asSolidStore(&doc.occStore());
                seedMembers(doc, store, /*withBeam=*/false);
                const EntityId mainId = doc.modelId(0);

                ComponentMaterializer mat(doc);
                const ComponentPlan cplan = componentPlanFromGenerationPlan(plan);
                ComponentMaterializer::Prepared prep = mat.prepare(cplan, mainId, {});
                check(prep.ok, "A prepare ok");
                ComponentInstanceRecord rec;
                rec.typeId = pkg.typeId;
                rec.schemaVersion = 1;
                rec.mainPart = mainId;
                const auto res = mat.commit(prep, rec);
                check(res.ok, "A commit ok");
                check(!res.ownedPlateIds.empty(), "A taban levhasi + kontur levhalari olustu");
                check(!res.ownedBoltGroupIds.empty(), "A bulon grubu belgeye eklendi");

                bool memberFree = true;      // ana uye hicbir grupta yok
                bool platesOnly = true;      // gruplarin parts'i yalniz levha
                bool modifiersPlatesOnly = true;
                std::size_t holeMods = 0;
                for (const EntityId bid : res.ownedBoltGroupIds) {
                    const auto bi = doc.indexOfBoltGroup(bid);
                    if (!bi) { platesOnly = false; continue; }
                    const BoltGroup& g = doc.boltGroups()[*bi];
                    if (contains(g.parts, mainId)) memberFree = false;
                    for (const EntityId p : g.parts)
                        if (!contains(res.ownedPlateIds, p)) platesOnly = false;
                    const auto mods = boltGroupHoleModifiers(g);
                    holeMods += mods.size();
                    for (const auto& m : mods)
                        if (!contains(res.ownedPlateIds, m.target)) modifiersPlatesOnly = false;
                }
                check(memberFree, "A ana uye (kolon) hicbir ankraj grubunda YOK");
                check(platesOnly, "A ankraj grubunun parts'i YALNIZ levhalar");
                check(holeMods > 0, "A levhalarda delik modifier'i uretildi");
                check(modifiersPlatesOnly, "A delik modifier'lari yalniz levhalari hedefler");

                // Kolonun geometrisi DEGISMEDI (delik uygulanmadi).
                const VersionId colVer = store->activeVersion(mainId);
                const VersionId colBase = store->baseVersion(mainId);
                check(colVer == colBase, "A kolon geometrisi degismedi (delik acilmadi)");
            }
        }
    }

    // --- B) NORMAL GRUP: alin levhasi paketi (endplate.placement -> beam_bolts)
    {
        ComponentRegistry scratch;
        registerBuiltInComponents(scratch);
        const ComponentPackageLoad pkg =
            loadComponentPackage(pkgs / "mm_stiffened_end_plate.mmcpkg", scratch);
        check(pkg.ok, "B alin levhasi paketi yuklendi");
        if (pkg.ok) {
            const ComponentInput in = makeInput(/*withBeam=*/true);
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, in);
            check(plan.ok, "B plan uretildi");
            if (plan.ok) {
                int mainGroups = 0;
                for (const auto& g : plan.boltGroups)
                    if (hasRole(g.partRoles, "main")) ++mainGroups;
                check(mainGroups >= 1, "B plan: uyeyi delEN normal grup var (parts = main)");

                Document doc;
                doc.setOccStore(makeOccSolidStore());
                OccSolidStore* store = asSolidStore(&doc.occStore());
                seedMembers(doc, store, /*withBeam=*/true);
                const EntityId mainId = doc.modelId(0);

                ComponentMaterializer mat(doc);
                const ComponentPlan cplan = componentPlanFromGenerationPlan(plan);
                ComponentMaterializer::Prepared prep = mat.prepare(cplan, mainId, {});
                check(prep.ok, "B prepare ok");
                ComponentInstanceRecord rec;
                rec.typeId = pkg.typeId;
                rec.schemaVersion = 1;
                rec.mainPart = mainId;
                const auto res = mat.commit(prep, rec);
                check(res.ok, "B commit ok");

                bool memberDrilled = false;
                for (const EntityId bid : res.ownedBoltGroupIds) {
                    const auto bi = doc.indexOfBoltGroup(bid);
                    if (!bi) continue;
                    const auto mods = boltGroupHoleModifiers(doc.boltGroups()[*bi]);
                    for (const auto& m : mods)
                        if (m.target == mainId) memberDrilled = true;
                }
                check(memberDrilled, "B ana uyede delik modifier'i VAR (normal grup uyeyi deler)");
            }
        }
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
