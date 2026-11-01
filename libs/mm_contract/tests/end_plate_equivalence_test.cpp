// MM-3b — End plate paketi ES-DEGERLIK testi + GOLDEN dogrulama (Task 0).
//
// Degismez kural: uyum YALNIZCA digest esitligi ile olculur.
//   5 sabit girdi icin:  generationPlanDigest(generateEndPlate(in))
//                        == generationPlanDigest(runRecipe(recipe, specs, in))
//                        == tests/golden/end_plate_digests.json degeri.
// recipe = packages/end_plate/recipe.json'in bellekteki karsiligi (tek
// endplate.placement adimi). Golden dosyasi DAVRANIS DEGISTIREN her commit'te
// gerekce yazilarak AYRI commit'te guncellenir; bu test asla yeniden uretmez.
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/recipe.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;
using json = nlohmann::json;

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

static std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<CompParamSpec> specs = endPlateComponentType().params;
    const Recipe recipe = endPlateRecipe();
    const char* names[5] = {"endplate_beam_px", "endplate_beam_nx", "endplate_beam_py",
                            "endplate_rotated_90", "endplate_diff_bolts"};

    json golden = json::object();
    for (int k = 0; k < 5; ++k) {
        const ComponentInput in = makeCase(k);
        const GenerationPlan gen = generateEndPlate(in);
        const GenerationPlan rec = runRecipe(recipe, specs, in);
        CHECK(gen.ok, "generateEndPlate ok");
        CHECK(rec.ok, "runRecipe ok");
        const std::string dGen = generationPlanDigest(gen);
        const std::string dRec = generationPlanDigest(rec);
        std::printf("  %-20s gen=%s rec=%s\n", names[k], dGen.c_str(), dRec.c_str());
        CHECK(dGen == dRec, "digest(runRecipe) == digest(generateEndPlate)");
        golden[names[k]] = dGen;
    }

    // GOLDEN dogrulama: dosya varsa birebir karsilastir (asla yeniden uretme);
    // yoksa uret (ilk baseline kurulumu — Task 0).
    std::printf("=== golden dosyasi ===\n");
    const std::string goldenPath =
        std::string(MM_CONTRACT_GOLDEN_DIR) + "/end_plate_digests.json";
    {
        std::ifstream in(goldenPath);
        if (in.good()) {
            json onDisk;
            in >> onDisk;
            in.close();
            CHECK(onDisk.is_object() && onDisk.size() == 5, "golden: 5 girdi mevcut");
            for (const auto& n : names) {
                if (onDisk.contains(n)) {
                    CHECK(onDisk[n].get<std::string>() == golden[n].get<std::string>(),
                          "golden digest birebir eslesiyor");
                } else {
                    CHECK(false, "golden girdisi eksik");
                }
            }
        } else {
            std::ofstream out(goldenPath);
            out << golden.dump(2) << "\n";
            out.close();
            std::printf("  golden uretildi: %s\n", goldenPath.c_str());
        }
    }

    std::printf("end plate esdegerlik: %s (%d hata)\n", failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
