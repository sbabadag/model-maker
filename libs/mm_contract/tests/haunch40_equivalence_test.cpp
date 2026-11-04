// MM-3c C3 + MM-3d — Haunch 40 paketi ES-DEGERLIK + tas bagimsizligi testi.
//
// Degismez kural: uyum YALNIZCA digest esitligi ile olculur.
//   5 golden girdi icin: generationPlanDigest(runRecipe(haunch40 recipe))
//                        == generationPlanDigest(generateHaunch40(in))
//                        == tests/golden/haunch40_digests.json degeri.
// Golden dosyasi YENIDEN URETILMEZ; fark raporlanir.
//
// MM-3d: her tas kendi parametrelerini args'ta alir, frame ADIYLA gecirilir,
// role uretilen roleKey'i gosterir. Ayrica haunch.web tasi frame.plumb_face
// ciktisiyla TEK BASINA cagrilip plan uretebilmeli.
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/haunch40.hpp"
#include "model_maker/recipe.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static constexpr double kPi = 3.14159265358979323846;

static RecipeArg S(const std::string& t) { RecipeArg a; a.text = t; return a; }

// --- 5 golden girdi (component_codec_test.cpp ile ayni) ----------------------

static ComponentInput makeInput(double slopeDeg) {
    ComponentInput in;
    // E: main (kolon) kanonik kurulur — axisTo = baglanti ucu (tepe).
    const MemberSectionSpec colSec{300.0, 300.0, 11.0, 19.0, 0.0};
    in.main = memberGeometryFromProfile(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 3000.0},
                                        /*connectionAtA=*/false, Vec3{0.0, 1.0, 0.0},
                                        0.0, colSec);
    in.main.id = 1;

    // E: kiris kanonik kurulur — axisTo = baglanti ucu ({0,60,1500}).
    const MemberSectionSpec beamSec{100.0, 200.0, 5.6, 8.5, 0.0};
    const double t = std::tan(slopeDeg * kPi / 180.0);
    MemberGeometry beam = memberGeometryFromProfile(
        Vec3{0.0, 1200.0, 1500.0 + 1140.0 * t}, Vec3{0.0, 60.0, 1500.0},
        /*connectionAtA=*/false, Vec3{0.0, 0.0, 1.0}, 0.0, beamSec);
    beam.id = 2;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputRotatedColumn() {
    ComponentInput in;
    // E: main kolon (Top ankraj, asimetrik) kanonik kurulur.
    const MemberSectionSpec colSec{240.0, 230.0, 7.5, 12.0, -115.0};
    in.main = memberGeometryFromProfile(Vec3{-115.0, 12000.0, 0.0},
                                        Vec3{-115.0, 12000.0, 5000.0},
                                        /*connectionAtA=*/false, Vec3{0.0, 0.0, 1.0},
                                        90.0, colSec);
    in.main.id = 45;
    // E: kiris kanonik kurulur — baglanti ucu {0,12000,5000} (kolon tepesi).
    const MemberSectionSpec beamSec{180.0, 400.0, 8.6, 13.5, 0.0};
    MemberGeometry beam = memberGeometryFromProfile(
        Vec3{0.0, 12000.0, 5000.0}, Vec3{5997.780613671378, 12000.0, 5986.683682028266},
        /*connectionAtA=*/true, Vec3{0.0, 0.0, 1.0}, 0.0, beamSec);
    beam.id = 47;
    in.secondary = {beam};
    return in;
}

static ComponentInput makeInputManualEndPlateHeight() {
    ComponentInput in = makeInput(0.0);
    // E: ayni kiris, baglanti ucunu koruyarak yeniden kanonik kurulur.
    const MemberSectionSpec beamSec{100.0, 200.0, 5.6, 8.5, 0.0};
    MemberGeometry beam = memberGeometryFromProfile(
        Vec3{0.0, 1200.0, 3000.0}, Vec3{0.0, 60.0, 3000.0},
        /*connectionAtA=*/false, Vec3{0.0, 0.0, 1.0}, 0.0, beamSec);
    beam.id = 2;
    in.secondary[0] = beam;
    in.params = {CompParam{L"endPlateHeight", 600.0}};
    return in;
}

// --- packages/haunch40/recipe.json ile BIREBIR ayni recipe ------------------
// MM-3d: her tas kendi parametrelerini alir; "frame" adiyla cerceve; "role".
static Recipe haunch40Recipe() {
    Recipe r;
    r.recipeVersion = "1.0.0";
    auto add = [&](const char* id, const char* use) -> RecipeStep& {
        RecipeStep t;
        t.id = id;
        t.use = use;
        r.steps.push_back(t);
        return r.steps.back();
    };

    add("c", "frame.contact");

    {
        RecipeStep& p = add("p", "frame.plumb_face");
        p.args["frame"] = S("c");
        p.args["gap"] = S("0");
        p.args["endPlateThickness"] = S("20");
        p.args["plateMaterial"] = S("S235JR");
    }
    {
        RecipeStep& e = add("ep", "haunch.end_plate");
        e.role = "haunch_end_plate";
        auto N = [&](const char* k, const char* v) { e.args[k] = S(v); };
        N("frame", "p");
        N("haunchLength", "450"); N("haunchDepth", "240"); N("haunchTipDepth", "25");
        N("haunchWebThickness", "10"); N("haunchFlangeThickness", "10");
        N("endPlateThickness", "20"); N("gap", "0");
        N("boltEdgeLeft", "35"); N("boltEdgeRight", "35"); N("boltEdge", "35");
        N("endPlateWidth", "0"); N("endPlateHeight", "param.endPlateHeight");
        N("haunchFlangeWidth", "0");
        N("upperBoltCountX", "2"); N("upperBoltCountY", "2");
        N("upperBoltPitchX", "120"); N("upperBoltPitchY", "80");
        N("upperBoltDiameter", "20"); N("upperHoleDiameter", "22");
        N("upperBoltEdgeClear", "44");
        N("lowerBoltCountX", "2"); N("lowerBoltCountY", "2");
        N("lowerBoltPitchX", "120"); N("lowerBoltPitchY", "80");
        N("lowerBoltDiameter", "20"); N("lowerHoleDiameter", "22");
        N("lowerBoltEdgeClear", "44");
        N("plateMaterial", "S235JR");
    }
    {
        RecipeStep& w = add("web", "haunch.web");
        w.role = "haunch_web";
        auto N = [&](const char* k, const char* v) { w.args[k] = S(v); };
        N("frame", "p");
        N("haunchLength", "450"); N("haunchDepth", "240");
        N("haunchTipDepth", "25"); N("haunchWebThickness", "10");
    }
    {
        RecipeStep& f = add("flange", "haunch.flange");
        f.role = "haunch_lower_flange";
        auto N = [&](const char* k, const char* v) { f.args[k] = S(v); };
        N("frame", "p");
        N("haunchLength", "450"); N("haunchDepth", "240"); N("haunchTipDepth", "25");
        N("haunchFlangeThickness", "10"); N("haunchFlangeWidth", "0");
    }
    {
        RecipeStep& st = add("stiff", "haunch.stiffeners");
        auto N = [&](const char* k, const char* v) { st.args[k] = S(v); };
        N("frame", "ep");
        N("haunchLength", "450"); N("haunchDepth", "240"); N("haunchFlangeThickness", "10");
        N("topPlateThickness", "10"); N("stiffenerClipSize", "15");
        N("stiffenerThickness", "10"); N("addStiffenerThickness", "10");
        N("rafterBottomStiffenerThickness", "10"); N("comprStiffenerThickness", "10");
        N("morrisStiffenerThickness", "10"); N("diagStiffenerThickness", "10");
        N("diagStiffenerWidth", "80");
        N("createBeamStiffener", "0"); N("createAddStiffener", "0");
        N("createRafterBottomStiffener", "0"); N("createComprStiffener", "0");
        N("createMorrisStiffener", "0"); N("createDiagStiffener", "0");
        N("plateMaterial", "S235JR");
    }
    {
        RecipeStep& b = add("bolts", "bolts.haunch_groups");
        b.role = "haunch_bolts";
        auto N = [&](const char* k, const char* v) { b.args[k] = S(v); };
        N("frame", "ep");
        N("upperBoltCountX", "2"); N("upperBoltCountY", "2");
        N("upperBoltPitchX", "120"); N("upperBoltPitchY", "80");
        N("upperBoltDiameter", "20"); N("upperHoleDiameter", "22");
        N("upperBoltEdgeClear", "44");
        N("lowerBoltCountX", "2"); N("lowerBoltCountY", "2");
        N("lowerBoltPitchX", "120"); N("lowerBoltPitchY", "80");
        N("lowerBoltDiameter", "20"); N("lowerHoleDiameter", "22");
        N("lowerBoltEdgeClear", "44");
        N("endPlateThickness", "20"); N("gap", "0");
        N("boltEdgeLeft", "35"); N("boltEdgeRight", "35");
        N("grade", "8.8");
    }
    {
        RecipeStep& wd = add("welds", "haunch.welds");
        auto N = [&](const char* k, const char* v) { wd.args[k] = S(v); };
        N("frame", "ep");
        N("weldSize", "6"); N("topPlateThickness", "10"); N("createWelds", "1");
    }
    return r;
}

// MM-3d kabul: haunch.web'i YALNIZ frame.plumb_face ciktisi + kendi args'iyla calistir.
static Recipe webOnlyRecipe() {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep c; c.id = "c"; c.use = "frame.contact"; r.steps.push_back(c);
    RecipeStep p; p.id = "p"; p.use = "frame.plumb_face";
    p.args["frame"] = S("c"); p.args["gap"] = S("0");
    p.args["endPlateThickness"] = S("20"); p.args["plateMaterial"] = S("S235JR");
    r.steps.push_back(p);
    RecipeStep w; w.id = "web"; w.use = "haunch.web"; w.role = "haunch_web";
    w.args["frame"] = S("p");
    w.args["haunchLength"] = S("450"); w.args["haunchDepth"] = S("240");
    w.args["haunchTipDepth"] = S("25"); w.args["haunchWebThickness"] = S("10");
    r.steps.push_back(w);
    return r;
}

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

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<CompParamSpec> specs = haunch40ComponentType().params;
    const Recipe recipe = haunch40Recipe();

    const RecipeValidation v = validateRecipe(recipe, specs);
    CHECK(v.ok, "recipe gecerli (statik analiz)");
    if (!v.ok) std::printf("  recipe error: %ls\n", v.message.c_str());

    struct Case { const char* name; ComponentInput in; };
    const Case cases[5] = {
        {"haunch40_horizontal", makeInput(0.0)},
        {"haunch40_inclined_5deg", makeInput(5.0)},
        {"haunch40_inclined_10deg", makeInput(10.0)},
        {"haunch40_rotated_column_90", makeInputRotatedColumn()},
        {"haunch40_manual_endplate_height", makeInputManualEndPlateHeight()},
    };

    const std::string goldenJson =
        readText(std::string(MM_CONTRACT_GOLDEN_DIR) + "/haunch40_digests.json");
    CHECK(!goldenJson.empty(), "golden dosyasi okundu");

    for (const auto& c : cases) {
        // E: kanoniklik denetimi.
        CHECK(isCanonicalMemberGeometry(c.in.main), "main kanonik");
        CHECK(isCanonicalMemberGeometry(c.in.secondary.front()), "secondary kanonik");
        const GenerationPlan gen = generateHaunch40(c.in);
        const GenerationPlan rec = runRecipe(recipe, specs, c.in);
        CHECK(gen.ok, "generateHaunch40 ok");
        CHECK(rec.ok, "runRecipe ok");
        if (!rec.ok) std::printf("  runRecipe error: %ls\n", rec.error.c_str());
        const std::string dg = generationPlanDigest(gen);
        const std::string dr = generationPlanDigest(rec);
        const std::string dgold = goldenDigest(goldenJson, c.name);
        std::printf("  %-30s gen=%s rec=%s\n", c.name, dg.c_str(), dr.c_str());
        if (!dgold.empty()) CHECK(dg == dgold, "generateHaunch40 == golden");
        else CHECK(false, "golden girdisi bulunamadi");
        CHECK(dg == dr, "digest(runRecipe) == digest(generateHaunch40)");
    }

    // MM-3d kabul testi: haunch.web TEK BASINA (frame.plumb_face ciktisiyla).
    std::printf("=== MM-3d: haunch.web tek basina (plumb_face ciktisi) ===\n");
    {
        const Recipe wr = webOnlyRecipe();
        const RecipeValidation wv = validateRecipe(wr, specs);
        CHECK(wv.ok, "web-only recipe gecerli");
        const GenerationPlan wp = runRecipe(wr, specs, makeInput(0.0));
        CHECK(wp.ok, "web-only runRecipe ok");
        if (!wp.ok) std::printf("  web-only error: %ls\n", wp.error.c_str());
        CHECK(wp.plates.size() == 1, "web-only: tam 1 levha");
        CHECK(wp.plateKeys.size() == 1 && wp.plateKeys[0] == "haunch_web",
              "web-only: role 'haunch_web'");
        std::printf("  web-only plates=%zu keys=%s\n", wp.plates.size(),
                    wp.plateKeys.empty() ? "(yok)" : wp.plateKeys[0].c_str());
    }

    std::printf("haunch40 esdegerlik: %s (%d hata)\n", failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
