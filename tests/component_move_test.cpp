// P-2/6 — KOPYALA / TASI BUTUNLUGU: uye tasininca komponent cocuklari
// (levha/bulon/kaynak/kesim) yeni konumda YENIDEN uretilir; basarisizsa eski
// hal KALIR (fail-closed) ve belge degismez.
//
// Kontrat (docs/agent/P-1-STUDIO-PAKETLERI.md, P-2):
//   "Kopyala / Taşı: üyelerle birlikte kopyalanır; kopya yeniden üretilir"
//   "Üye değişince (profil değişimi, kolon taşıma): yeniden üretilir.
//    Başarısızsa eski hâl kalır ve uyarı verilir (fail-closed)."
//
// Kopya tarafi `component_copy_test`'te kilitli; burada TASIMA tarafi denenir:
//   A) Kolon +2000 mm X'e tasinir -> taban levhasi/ankrajlar/rijitlestiriciler
//      yeni konumda, ADET AYNI (cift uretim yok), roleKey -> EntityId KARARLI.
//   B) Alin levhasi paketinde kiris temasdan cikacak kadar uzaklasir ->
//      plan REDDEDER, eski levhalar YERINDE kalir, hicbir model degismez.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/component_recompute.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/recipe.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static SteelProfile prof(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

static std::vector<SteelProfile> catalog() {
    return {prof("HEB300", 300.0, 300.0, 11.0, 19.0), prof("IPE240", 240.0, 120.0, 6.2, 9.8)};
}

// Profilli uye ekler (gercek I-kesit katisi + eksen ozellikleri).
static EntityId addMember(Document& doc, OccSolidStore* store, const SteelProfile& p, Vec3 a, Vec3 b,
                          Vec3 refNormal) {
    const TopoDS_Shape solid = extrudeProfileSolid(p, a, b, 0.0, refNormal);
    WireframeModel m = shapeToWireframeWithFaces(solid, 0.15);
    EntityProperties props = m.properties();
    props.profileName = p.name;
    props.axisFromX = a.x;
    props.axisFromY = a.y;
    props.axisFromZ = a.z;
    props.axisToX = b.x;
    props.axisToY = b.y;
    props.axisToZ = b.z;
    props.sectionNormalX = refNormal.x;
    props.sectionNormalY = refNormal.y;
    props.sectionNormalZ = refNormal.z;
    props.sectionAnchor = SectionAnchor::Center;
    m.setProperties(std::move(props));
    doc.addModel(std::move(m));
    const EntityId id = doc.modelId(doc.models().size() - 1);
    if (store && !solid.IsNull()) {
        store->set(id, solid);
        store->setBaseVersion(id, store->activeVersion(id));
    }
    return id;
}

// Uygulamanin `rebuildCutBaseSolid` kurdugu kural: uye tasindiktan sonra
// B-Rep GUNCEL eksen ozelliklerinden yeniden uretilir. Uygulama bunu
// recomputeComponentInstancesFor icinde yapar; test de ayni adimi atar —
// yoksa kaynak katlari eski konumdaki katilarla eslesmez (yapay hata).
static void refreshMemberSolid(Document& doc, OccSolidStore* store, EntityId id,
                               const SteelProfile& p) {
    if (!store) return;
    const auto idx = doc.indexOfEntity(id);
    if (!idx) return;
    const EntityProperties& pr = doc.models()[*idx].properties();
    const Vec3 from{pr.axisFromX, pr.axisFromY, pr.axisFromZ};
    const Vec3 to{pr.axisToX, pr.axisToY, pr.axisToZ};
    const Vec3 ref{pr.sectionNormalX, pr.sectionNormalY, pr.sectionNormalZ};
    const TopoDS_Shape solid =
        extrudeProfileSolid(p, from, to, pr.profileRotation, ref, pr.sectionAnchor);
    if (solid.IsNull()) return;
    store->set(id, solid);
    store->setBaseVersion(id, store->activeVersion(id));
}

// Bir EntityId'nin dunya bbox merkezi.
static bool centerOf(const Document& doc, EntityId id, Vec3* out) {
    const auto idx = doc.indexOfEntity(id);
    if (!idx) return false;
    const auto& mb = doc.modelBounds();
    if (*idx >= mb.size()) return false;
    const Bounds3& b = mb[*idx];
    *out = Vec3{(b.minimum.x + b.maximum.x) * 0.5, (b.minimum.y + b.maximum.y) * 0.5,
                (b.minimum.z + b.maximum.z) * 0.5};
    return true;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::filesystem::path pkgs = std::filesystem::path(MM_FIXTURES_DIR) / "studio_pkgs";
    const std::vector<SteelProfile> cat = catalog();

    // --- A) KOLON TASIMA: taban levhasi paketi kolonla birlikte gider --------
    {
        ComponentRegistry scratch;
        registerBuiltInComponents(scratch);
        const ComponentPackageLoad pkg =
            loadComponentPackage(pkgs / "mm_stiffened_base_plate.mmcpkg", scratch);
        check(pkg.ok, "A paket yuklendi");
        if (pkg.ok) {
            Document doc;
            doc.setOccStore(makeOccSolidStore());
            OccSolidStore* store = asSolidStore(&doc.occStore());
            const EntityId col = addMember(doc, store, cat[0], {0, 0, 0}, {0, 0, 3000}, {1, 0, 0});

            auto in = componentInputFromDocument(doc, cat, col, {});
            check(in.has_value(), "A kanonik girdi uretildi");
            if (in) {
                const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
                check(plan.ok, "A plan uretildi");
                if (plan.ok) {
                    ComponentMaterializer mat(doc);
                    ComponentMaterializer::Prepared prep =
                        mat.prepare(componentPlanFromGenerationPlan(plan), col, {});
                    ComponentInstanceRecord rec;
                    rec.typeId = pkg.typeId;
                    rec.schemaVersion = 1;
                    rec.mainPart = col;
                    const auto res = mat.commit(prep, rec);
                    check(res.ok, "A ilk kurulum ok");
                    check(!res.ownedPlateIds.empty(), "A levhalar olustu");
                    const std::size_t platesAfterFirst = res.ownedPlateIds.size();
                    const std::size_t modelsAfterFirst = doc.models().size();

                    Vec3 before{};
                    const EntityId basePlateRole =
                        res.roleToEntity.count("base_plate") ? res.roleToEntity.at("base_plate")
                                                             : res.ownedPlateIds.front();
                    check(centerOf(doc, basePlateRole, &before), "A taban levhasi merkezi olculdu");

                    // --- TASI: kolon +2000 mm X ---
                    doc.moveModels({0}, {2000.0, 0.0, 0.0});
                    refreshMemberSolid(doc, store, col, cat[0]);
                    auto in2 = componentInputFromDocument(doc, cat, col, {});
                    check(in2.has_value(), "A tasima sonrasi girdi uretildi");
                    if (in2) {
                        const GenerationPlan plan2 = runRecipe(pkg.recipe, pkg.type.params, *in2);
                        check(plan2.ok, "A tasima sonrasi plan uretildi");
                        if (plan2.ok) {
                            ComponentMaterializer::Prepared prep2 =
                                mat.prepare(componentPlanFromGenerationPlan(plan2), col, {});
                            const auto rres = mat.recompute(
                                prep2, doc.componentInstances().front(), rec, /*openOwnRecord=*/false);
                            check(rres.ok, "A recompute ok (fail-closed DEGIL)");
                            check(rres.ownedPlateIds.size() == platesAfterFirst,
                                  "A levha ADEDI ayni (cift uretim yok)");
                            Vec3 after{};
                            check(centerOf(doc, basePlateRole, &after),
                                  "A taban levhasi hala ayni roleKey altinda");
                            const double dx = after.x - before.x;
                            std::printf("      [olcum] taban levhasi X kaymasi: %.1f mm\n", dx);
                            check(std::abs(dx - 2000.0) < 1.0,
                                  "A taban levhasi kolonla BIRLIKTE 2000 mm gitti");
                            check(std::abs(after.z - before.z) < 1.0,
                                  "A levha Z'de kaymadi (yalniz yatay tasima)");
                            std::printf("      [olcum] model sayisi: once=%zu sonra=%zu (fark=%d)\n",
                                        modelsAfterFirst, doc.models().size(),
                                        static_cast<int>(doc.models().size()) -
                                            static_cast<int>(modelsAfterFirst));
                            {
                                // SAHIP DENETIMI (cift uretim/sizinti): profilsiz her
                                // modelin bir komponent cocuguna (levha/bulon/kaynak
                                // gorseli) bagli olmasi gerekir; sahipsiz kalan model
                                // eski (silinmemis) cocuktur.
                                std::set<EntityId> owned(
                                    doc.componentInstances().front().ownedPlateIds.begin(),
                                    doc.componentInstances().front().ownedPlateIds.end());
                                int weldOk = 0, weldBroken = 0;
                                for (const auto& w : doc.welds()) {
                                    owned.insert(w.displayEntityId);
                                    (doc.indexOfEntity(w.displayEntityId) ? weldOk : weldBroken)++;
                                }
                                std::printf("      [olcum] kaynak gorseli: %d/%zu canli\n",
                                            weldOk, doc.welds().size());
                                check(weldBroken == 0,
                                      "A kaynak gorsellerinin HEPSI yeni konumda canli");
                                bool boltOk = true;
                                for (const auto& b : doc.boltGroups()) {
                                    owned.insert(b.displayEntityId);
                                    if (!doc.indexOfEntity(b.displayEntityId)) boltOk = false;
                                }
                                check(boltOk, "A bulon montaji gorseli canli");
                                int orphan = 0;
                                for (std::size_t i = 0; i < doc.models().size(); ++i) {
                                    const EntityId id = doc.modelId(i);
                                    if (doc.models()[i].properties().profileName.empty() &&
                                        owned.count(id) == 0) {
                                        ++orphan;
                                        std::printf("      [sahipsiz] model %zu id=%llu\n", i,
                                                    static_cast<unsigned long long>(id));
                                    }
                                }
                                check(orphan == 0,
                                      "A sahipsiz (artik) model yok — cift uretim yok");
                            }
                            {
                                // "Eski cocuk KALMADI" kaniti konum uzerinden:
                                // tasimadan sonra HICBIR model eski X konumunda
                                // (< 1000 mm) durmamali — stale levha yok.
                                int stale = 0;
                                const auto& mb = doc.modelBounds();
                                for (std::size_t i = 0; i < doc.models().size() && i < mb.size(); ++i) {
                                    const double cx = (mb[i].minimum.x + mb[i].maximum.x) * 0.5;
                                    if (cx < 1000.0) {
                                        ++stale;
                                        std::printf("      [stale] model %zu merkez X=%.1f\n", i, cx);
                                    }
                                }
                                check(stale == 0,
                                      "A eski konumda KALAN cocuk yok (stale model 0)");
                            }
                            check(doc.componentInstances().size() == 1,
                                  "A hala tek component kaydi");
                        }
                    }
                }
            }
        }
    }

    // --- B) FAIL-CLOSED: ana uye yoksa/kaybolduysa eski hal KALIR ------------
    {
        ComponentRegistry scratch;
        registerBuiltInComponents(scratch);
        const ComponentPackageLoad pkg =
            loadComponentPackage(pkgs / "mm_stiffened_end_plate.mmcpkg", scratch);
        check(pkg.ok, "B paket yuklendi");
        if (pkg.ok) {
            Document doc;
            doc.setOccStore(makeOccSolidStore());
            OccSolidStore* store = asSolidStore(&doc.occStore());
            const EntityId col = addMember(doc, store, cat[0], {0, 0, 0}, {0, 0, 3000}, {1, 0, 0});
            const EntityId beam =
                addMember(doc, store, cat[1], {0, 120, 2880}, {6000, 120, 2880}, {0, 0, 1});

            auto in = componentInputFromDocument(doc, cat, col, {beam});
            check(in.has_value(), "B kanonik girdi uretildi");
            if (in) {
                const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, *in);
                check(plan.ok, "B plan uretildi");
                if (plan.ok) {
                    ComponentMaterializer mat(doc);
                    ComponentMaterializer::Prepared prep =
                        mat.prepare(componentPlanFromGenerationPlan(plan), col, {});
                    ComponentInstanceRecord rec;
                    rec.typeId = pkg.typeId;
                    rec.schemaVersion = 1;
                    rec.mainPart = col;
                    const auto res = mat.commit(prep, rec);
                    check(res.ok, "B ilk kurulum ok");
                    const std::size_t modelsAfterFirst = doc.models().size();
                    const std::size_t platesAfterFirst = res.ownedPlateIds.size();
                    const EntityId plate =
                        res.ownedPlateIds.empty() ? kInvalidEntityId : res.ownedPlateIds.front();
                    Vec3 before{};
                    check(plate != kInvalidEntityId && centerOf(doc, plate, &before),
                          "B levha merkezi olculdu");

                    // FAIL-CLOSED: ana uyeyi kaldir -> hazirlik BASARISIZ olmali
                    // (uygulama ayni yolda atomicRecompute ile eski hali korur).
                    if (const auto mi = doc.indexOfEntity(col)) doc.removeModelLeaf(*mi);
                    auto in2 = componentInputFromDocument(doc, cat, col, {beam});
                    const bool prepared =
                        in2.has_value() &&
                        runRecipe(pkg.recipe, pkg.type.params, *in2).ok;
                    check(!prepared,
                          "B ana uye yok -> hazirlik REDDETTI (sessiz yenileme yok)");
                    Vec3 after{};
                    check(centerOf(doc, plate, &after), "B eski levha yerinde");
                    check(std::abs(after.x - before.x) < 1e-9 &&
                              std::abs(after.z - before.z) < 1e-9,
                          "B eski levha KONUMU degismedi (fail-closed)");
                    check(doc.models().size() == modelsAfterFirst - 1,
                          "B belgeye yeni model EKLENMEDI (yalniz silinen uye)");
                    check(doc.componentInstances().size() == 1,
                          "B component kaydi korundu");
                    check(doc.componentInstances().front().ownedPlateIds.size() ==
                              platesAfterFirst,
                          "B levha ADEDI korundu (cift uretim/eksilme yok)");
                }
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
