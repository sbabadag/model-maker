// MM-3b — End plate paketi ES-DEGERLIK testi.
//
// Degismez kural: uyum YALNIZCA digest esitligi ile olculur.
//   5 sabit girdi icin:  generationPlanDigest(generateEndPlate(in))
//                        == generationPlanDigest(runRecipe(recipe, specs, in))
// recipe = packages/end_plate/recipe.json'in bellekteki karsiligi (tek
// endplate.placement adimi). Golden dosyasi YENIDEN URETILMEZ; fark raporlanir.
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/recipe.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static RecipeArg S(const std::string& t) { RecipeArg a; a.text = t; return a; }

// packages/end_plate/recipe.json ile BIREBIR ayni recipe.
static Recipe endPlateRecipe() {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep s;
    s.id = "ep";
    s.use = "endplate.placement";
    s.args["main"] = S("main");
    s.args["beam"] = S("secondary_0");
    s.args["rolePlate"] = S("end_plate");
    s.args["roleBolts"] = S("beam_bolts");
    r.steps.push_back(s);
    return r;
}

// 5 sabit girdi: (0) +X, (1) -X, (2) +Y, (3) 90° donuk profil, (4) farkli bulon.
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
            break;
        default:
            break;
    }
    in.secondary.push_back(b);
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<CompParamSpec> specs = endPlateComponentType().params;
    const Recipe recipe = endPlateRecipe();
    const char* names[5] = {"beam +X", "beam -X", "beam +Y", "90deg profil", "farkli bulon"};

    for (int k = 0; k < 5; ++k) {
        const ComponentInput in = makeCase(k);
        const GenerationPlan gen = generateEndPlate(in);
        const GenerationPlan rec = runRecipe(recipe, specs, in);
        CHECK(gen.ok, "generateEndPlate ok");
        CHECK(rec.ok, "runRecipe ok");
        const std::string dGen = generationPlanDigest(gen);
        const std::string dRec = generationPlanDigest(rec);
        std::printf("  %-16s gen=%s rec=%s\n", names[k], dGen.c_str(), dRec.c_str());
        CHECK(dGen == dRec, "digest(runRecipe) == digest(generateEndPlate)");
    }

    std::printf("end plate esdegerlik: %s (%d hata)\n", failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
