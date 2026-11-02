// MM-5 fix (item 1) — PAKET RECIPE'INDEN PARAMETRE AKISI testi (OCC GEREKTIRMEZ).
//
// Sorun: packages/haunch40/recipe.json taslarinda parametreler SABIT yazilmisti
// ("450", "S235JR" ...); diyalogdan deger degistirmek geometriyi degistirmiyordu.
// Duzeltme: her tas kendi argumanlarini `param.<ad>` ifadesiyle okur.
//
// Bu test PAKETTEKI recipe.json'u (tek kaynak) yukler ve olcer:
//   A) 5 golden girdi: digest(recipe) == digest(generateHaunch40) == golden dosyasi
//      (golden dosyasi DEGISMEZ — parametrelerin varsayilanlari eski sabitlerle ayni).
//   B) en az 5 parametre degistirilmis girdiler: digest(recipe) == digest(generateHaunch40).
// 5 golden girdi tanimi haunch40_equivalence_test.cpp ile AYNIDIR (ayni fixture).
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/haunch40.hpp"
#include "model_maker/recipe.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static constexpr double kPi = 3.14159265358979323846;

#ifndef MM_PACKAGES_DIR
#error "MM_PACKAGES_DIR tanimli olmali"
#endif
#ifndef MM_CONTRACT_GOLDEN_DIR
#error "MM_CONTRACT_GOLDEN_DIR tanimli olmali"
#endif

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

// --- 5 golden girdi (haunch40_equivalence_test.cpp ile ayni) -----------------
static ComponentInput makeInput(double slopeDeg) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 1, 0};
    in.main.secHeight = 300;
    in.main.secWidth = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;
    in.main.sectionCenterOffset = 0.0;

    MemberGeometry beam;
    beam.id = 2;
    const double t = std::tan(slopeDeg * kPi / 180.0);
    beam.axisFrom = {0, 1200, 1500 + 1140 * t};
    beam.axisTo = {0, 60, 1500};
    beam.refNormal = {0, 0, 1};
    beam.secHeight = 200;
    beam.secWidth = 100;
    beam.secWebThickness = 5.6;
    beam.secFlangeThickness = 8.5;
    beam.sectionCenterOffset = 0.0;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputRotatedColumn() {
    ComponentInput in;
    in.main.id = 45;
    in.main.axisFrom = {-115.0, 12000.0, 0.0};
    in.main.axisTo = {-115.0, 12000.0, 5000.0};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 240.0;
    in.main.secHeight = 230.0;
    in.main.secWebThickness = 7.5;
    in.main.secFlangeThickness = 12.0;
    in.main.sectionCenterOffset = -115.0;
    in.main.profileRotation = 90.0;
    MemberGeometry beam;
    beam.id = 47;
    beam.axisFrom = {0.0, 12000.0, 5000.0};
    beam.axisTo = {5997.780613671378, 12000.0, 5986.683682028266};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 180.0;
    beam.secHeight = 400.0;
    beam.secWebThickness = 8.6;
    beam.secFlangeThickness = 13.5;
    beam.sectionCenterOffset = 0.0;
    beam.profileRotation = 0.0;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputManualEndPlateHeight() {
    ComponentInput in = makeInput(0.0);
    in.secondary[0].axisFrom = {0, 1200, 3000};
    in.secondary[0].axisTo = {0, 60, 3000};
    in.params = {CompParam{L"endPlateHeight", 600.0}};
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<CompParamSpec> specs = haunch40ComponentType().params;

    // (0) Recipe PAKETTEN yuklenir — test edilen sey kullanicinin aldigi dosyadir.
    const std::string recipeText =
        readText(std::string(MM_PACKAGES_DIR) + "/haunch40/recipe.json");
    check(!recipeText.empty(), "0 packages/haunch40/recipe.json okundu");
    if (recipeText.empty()) {
        std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
        return 1;
    }
    const Recipe recipe = recipeFromJson(nlohmann::json::parse(recipeText, nullptr, false));
    const RecipeValidation v = validateRecipe(recipe, specs);
    check(v.ok, "0 paket recipe gecerli (statik analiz)");
    if (!v.ok) std::printf("   recipe error: %ls\n", v.message.c_str());

    // (0b) Sabit yazim kalmadi: her tas argumanlari param.* ya da frame/role.
    {
        int paramArgs = 0, literalNumeric = 0;
        for (const auto& st : recipe.steps) {
            for (const auto& kv : st.args) {
                const std::string& key = kv.first;
                const std::string& val = kv.second.text;
                if (key == "frame" || key == "plateMaterial" || key == "grade" ||
                    key == "role" || key == "main" || key == "secondary")
                    continue;
                if (val.rfind("param.", 0) == 0) ++paramArgs;
                else ++literalNumeric;
            }
        }
        std::printf("   param.* arguman=%d, literal sayisal arguman=%d\n", paramArgs, literalNumeric);
        check(paramArgs >= 40, "0b taslarin argumanlari param.* ifadeleri");
        check(literalNumeric == 0, "0b sabit sayisal arguman KALMADI");
    }

    // (A) Golden: paket recipe == generateHaunch40 == golden dosyasi.
    const std::string goldenJson =
        readText(std::string(MM_CONTRACT_GOLDEN_DIR) + "/haunch40_digests.json");
    check(!goldenJson.empty(), "A golden dosyasi okundu");
    struct Case { const char* name; ComponentInput in; };
    const Case goldens[5] = {
        {"haunch40_horizontal", makeInput(0.0)},
        {"haunch40_inclined_5deg", makeInput(5.0)},
        {"haunch40_inclined_10deg", makeInput(10.0)},
        {"haunch40_rotated_column_90", makeInputRotatedColumn()},
        {"haunch40_manual_endplate_height", makeInputManualEndPlateHeight()},
    };
    for (const auto& c : goldens) {
        const GenerationPlan gen = generateHaunch40(c.in);
        const GenerationPlan rec = runRecipe(recipe, specs, c.in);
        check(gen.ok, "A generateHaunch40 ok");
        check(rec.ok, "A runRecipe ok");
        if (!rec.ok) std::printf("   %s runRecipe error: %ls\n", c.name, rec.error.c_str());
        const std::string dg = generationPlanDigest(gen);
        const std::string dr = generationPlanDigest(rec);
        const std::string dgold = goldenDigest(goldenJson, c.name);
        std::printf("   %-30s gen=%s golden=%s\n", c.name, dg.c_str(), dgold.c_str());
        check(!dgold.empty(), "A golden girdisi bulundu");
        check(dg == dgold, "A generateHaunch40 == golden (DEGISMEDI)");
        check(dg == dr, "A digest(recipe) == digest(generateHaunch40)");
    }

    // (B) Parametre akisi: >=5 parametre degistir, recipe == generator.
    auto withParams = [](ComponentInput in, bool lower) {
        if (!lower) {
            in.params.push_back(CompParam{L"haunchLength", CompParamValue{520.0}});
            in.params.push_back(CompParam{L"haunchDepth", CompParamValue{270.0}});
            in.params.push_back(CompParam{L"endPlateThickness", CompParamValue{15.0}});
            in.params.push_back(CompParam{L"upperBoltPitchX", CompParamValue{140.0}});
            in.params.push_back(CompParam{L"upperBoltDiameter", CompParamValue{24.0}});
            in.params.push_back(CompParam{L"upperHoleDiameter", CompParamValue{26.0}});
            in.params.push_back(CompParam{L"plateMaterial", CompParamValue{std::wstring(L"S355JR")}});
        } else {
            in.params.push_back(CompParam{L"lowerBoltCountX", CompParamValue{3}});
            in.params.push_back(CompParam{L"lowerBoltPitchX", CompParamValue{90.0}});
            in.params.push_back(CompParam{L"lowerBoltDiameter", CompParamValue{16.0}});
            in.params.push_back(CompParam{L"lowerHoleDiameter", CompParamValue{18.0}});
            in.params.push_back(CompParam{L"lowerBoltEdgeClear", CompParamValue{38.0}});
            in.params.push_back(CompParam{L"createWelds", CompParamValue{false}});
            in.params.push_back(CompParam{L"grade", CompParamValue{std::wstring(L"10.9")}});
        }
        return in;
    };
    const Case varied[4] = {
        {"params_upper_horizontal", withParams(makeInput(0.0), false)},
        {"params_lower_inclined_10", withParams(makeInput(10.0), true)},
        {"params_upper_inclined_5", withParams(makeInput(5.0), false)},
        {"params_lower_horizontal", withParams(makeInput(0.0), true)},
    };
    for (const auto& c : varied) {
        const GenerationPlan gen = generateHaunch40(c.in);
        const GenerationPlan rec = runRecipe(recipe, specs, c.in);
        check(gen.ok, "B generateHaunch40 ok");
        check(rec.ok, "B runRecipe ok");
        if (!rec.ok) std::printf("   %s runRecipe error: %ls\n", c.name, rec.error.c_str());
        const std::string dg = generationPlanDigest(gen);
        const std::string dr = generationPlanDigest(rec);
        std::printf("   %-30s gen=%s rec=%s\n", c.name, dg.c_str(), dr.c_str());
        check(dg == dr, "B degisen parametrelerde recipe == generator");
        check(goldenDigest(goldenJson, c.name).empty(),
              "B parametreli girdi golden dosyasinda YOK (golden degismez)");
        // Parametrelerin gercekten geometriye ulastiginin kaniti: gen digest'i
        // ayni girdinin parametresiz halinden FARKLI olmali.
        const GenerationPlan base = generateHaunch40(
            c.name == std::string("params_upper_horizontal") ? makeInput(0.0)
            : c.name == std::string("params_lower_inclined_10") ? makeInput(10.0)
            : c.name == std::string("params_upper_inclined_5") ? makeInput(5.0)
                                                                : makeInput(0.0));
        check(dg != generationPlanDigest(base), "B parametreler geometriyi DEGISTIRDI");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
