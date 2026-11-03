// ST-4 (GUI-3) — MODELDE LEVHA RENGI: her Plate varligi kPlateGreyRgb olmali.
//
// Windows GUI'de bazi levhalar (Haunch 40 govde levhasi, End Plate paket levhasi)
// YESIL gorunuyordu. Bu test gercek OCCT materializer uzerinden iki yolu kurar
// (yerlesik Haunch 40 generatoru + paket recipe'si) ve commit sonrasi tum
// component-owned Plate modellerinin effectiveColor'unu denetler.
//
// Secim/hover vurgusu RENDER zamaninda cizilir (kGpuSelectionHighlight) ve
// modelde saklanan renge dokunmaz; bu yuzden burada aranan tek sey DEPODA
// SAKLANAN renktir.

#include "model_maker/component_codec.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/recipe.hpp"

#include "mm_contract/component_visual.hpp"

#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0.0, 0.0, 0.0};
    in.main.axisTo = {0.0, 0.0, 3000.0};
    in.main.refNormal = {0.0, 0.0, 1.0};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;
    MemberGeometry b;
    b.id = 2;
    b.axisFrom = {0.0, 0.0, 3000.0};
    b.axisTo = {2000.0, 0.0, 3000.0};
    b.refNormal = {0.0, 0.0, 1.0};
    b.secWidth = 200.0;
    b.secHeight = 400.0;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary.push_back(b);
    return in;
}

// Document'a ana/ikincil parcalari (gercek B-Rep ile) koyar.
static void seedDocument(Document& doc, OccSolidStore* store, const ComponentInput& in) {
    ComponentMaterializer::Prepared dummy;
    (void)dummy;
    WireframeModel col = WireframeModel::cube(100.0);
    doc.addModel(std::move(col));
    WireframeModel beam = WireframeModel::cube(100.0);
    doc.addModel(std::move(beam));
    if (!store) return;
    for (std::size_t i = 0; i < 2; ++i) {
        PlateSpec spec;
        spec.center = {0.0, 0.0, i == 0 ? 0.0 : 3000.0};
        spec.u = {1.0, 0.0, 0.0};
        spec.v = {0.0, 1.0, 0.0};
        spec.axis = {0.0, 0.0, 1.0};
        spec.width = 300.0;
        spec.height = 300.0;
        spec.thickness = 300.0;
        const TopoDS_Shape solid = ComponentMaterializer::buildPlateSolid(spec);
        if (!solid.IsNull()) {
            const EntityId id = doc.modelId(i);
            store->set(id, solid);
            store->setBaseVersion(id, store->activeVersion(id));
        }
    }
    (void)in;
}

static void checkPlateColors(Document& doc, const std::vector<EntityId>& plateIds,
                             const char* label) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s: en az bir levha uretildi", label);
    check(!plateIds.empty(), buf);
    for (const EntityId id : plateIds) {
        const auto idx = doc.indexOfEntity(id);
        std::snprintf(buf, sizeof(buf), "%s: levha #%llu modelde var", label,
                      static_cast<unsigned long long>(id));
        if (!idx) { check(false, buf); continue; }
        const auto color = doc.models()[*idx].properties().effectiveColor;
        std::snprintf(buf, sizeof(buf), "%s: levha #%llu rengi kPlateGreyRgb (0x%06X, gercek 0x%06X)",
                      label, static_cast<unsigned long long>(id), kPlateGreyRgb, color);
        check(color == kPlateGreyRgb, buf);
    }
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const ComponentInput in = makeInput();

    // --- 1) Yerlesik Haunch 40 (govde levhasi dahil) ------------------------
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        seedDocument(doc, store, in);

        ComponentInput hin = in;
        for (const auto& spec : haunch40ComponentType().params)
            hin.params.push_back(CompParam{spec.name, paramDefaultValue(spec)});

        const GenerationPlan plan = generateHaunch40(hin);
        check(plan.ok, "haunch40: plan uretildi");
        if (plan.ok) {
            ComponentMaterializer mat(doc);
            const ComponentPlan cplan = componentPlanFromGenerationPlan(plan);
            ComponentMaterializer::Prepared prep = mat.prepare(cplan, doc.modelId(0), {});
            check(prep.ok, "haunch40: prepare ok");
            ComponentInstanceRecord rec;
            rec.typeId = L"tekla_haunch_40";
            rec.schemaVersion = 1;
            rec.mainPart = doc.modelId(0);
            const auto res = mat.commit(prep, rec);
            check(res.ok, "haunch40: commit ok");
            checkPlateColors(doc, res.ownedPlateIds, "haunch40");
        }
    }

    // --- 2) Paket recipe'si (End Plate paketi) ------------------------------
    {
        Document doc;
        doc.setOccStore(makeOccSolidStore());
        OccSolidStore* store = asSolidStore(&doc.occStore());
        seedDocument(doc, store, in);

        ComponentRegistry scratch;
        registerBuiltInComponents(scratch);
        const ComponentPackageLoad pkg =
            loadComponentPackage(std::filesystem::path(MM_PACKAGES_DIR) / "end_plate_pkg", scratch);
        check(pkg.ok, "end_plate_pkg: paket yuklendi");
        if (pkg.ok) {
            const GenerationPlan plan = runRecipe(pkg.recipe, pkg.type.params, in);
            check(plan.ok, "end_plate_pkg: recipe plan uretildi");
            if (plan.ok) {
                ComponentMaterializer mat(doc);
                const ComponentPlan cplan = componentPlanFromGenerationPlan(plan);
                ComponentMaterializer::Prepared prep = mat.prepare(cplan, doc.modelId(0), {});
                check(prep.ok, "end_plate_pkg: prepare ok");
                ComponentInstanceRecord rec;
                rec.typeId = pkg.typeId;
                rec.schemaVersion = 1;
                rec.mainPart = doc.modelId(0);
                const auto res = mat.commit(prep, rec);
                check(res.ok, "end_plate_pkg: commit ok");
                checkPlateColors(doc, res.ownedPlateIds, "end_plate_pkg");
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
