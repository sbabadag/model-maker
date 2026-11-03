// MM-3a — RECIPE YORUMLAYICI testi (OCCT/Qt'siz, Linux + Windows mingw).
// Kapsam: (1) ifade degerlendirici, (2) param.*/main.*/secondary_0.* erisimi,
// (3) statik analiz (bilinmeyen blok/parametre/let, dongusel let, roleKey
// tekilligi/tur tutarliligi/rezerve, adim siniri), (4) v0 taslarinin plan
// uretimi, (5) fail-closed check, (6) frame.offset geometrisi.
#include "model_maker/recipe.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "mm_contract/contract_version.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static RecipeArg S(const std::string& t) { RecipeArg a; a.text = t; return a; }
static RecipeArg L(std::vector<std::string> xs) {
    RecipeArg a; a.isList = true;
    for (const auto& x : xs) { RecipeArg c; c.text = x; a.items.push_back(c); }
    return a;
}

// Standart girdi: kolon (main) + kiris (secondary_0).
static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = Vec3{0.0, 0.0, 0.0};
    in.main.axisTo = Vec3{0.0, 0.0, 3000.0};
    in.main.refNormal = Vec3{0.0, 0.0, 1.0};
    in.main.secWidth = 300.0;    // b
    in.main.secHeight = 300.0;   // h
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;
    MemberGeometry beam;
    beam.id = 2;
    beam.axisFrom = Vec3{0.0, 0.0, 3000.0};
    beam.axisTo = Vec3{6000.0, 0.0, 3000.0};
    beam.refNormal = Vec3{0.0, 0.0, 1.0};
    beam.secWidth = 150.0;
    beam.secHeight = 300.0;
    beam.secWebThickness = 7.1;
    beam.secFlangeThickness = 10.7;
    in.secondary.push_back(beam);
    return in;
}

// Bir ifadeyi, tek levhanin genisligi olarak degerlendirir (hata -> NAN).
static double evalNum(const std::string& expr, const ComponentInput& in,
                      const std::vector<CompParamSpec>& specs = {}) {
    Recipe r;
    r.lets.push_back({"X", expr});
    RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
    RecipeStep p; p.id = "p"; p.use = "plate.rect";
    p.args["frame"] = S("f");
    p.args["w"] = S("X");
    p.args["h"] = S("1");
    p.args["t"] = S("1");
    r.steps.push_back(f);
    r.steps.push_back(p);
    const GenerationPlan plan = runRecipe(r, specs, in);
    if (!plan.ok || plan.plates.empty()) return std::nan("");
    return plan.plates[0].width;
}

static bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

static void testExpressionEvaluator() {
    const ComponentInput in = makeInput();
    CHECK(near(evalNum("1+2*3", in), 7.0), "oncelik: 1+2*3=7");
    CHECK(near(evalNum("(1+2)*3", in), 9.0), "parantez");
    CHECK(near(evalNum("2^3^2", in), 512.0), "us sag-birlesik 2^3^2=512");
    CHECK(near(evalNum("min(3,1,2)", in), 1.0), "min variadic");
    CHECK(near(evalNum("max(3,1,2)", in), 3.0), "max variadic");
    CHECK(near(evalNum("abs(-5)", in), 5.0), "abs");
    CHECK(near(evalNum("sqrt(16)", in), 4.0), "sqrt");
    CHECK(near(evalNum("sin(30)", in), 0.5), "sin derece");
    CHECK(near(evalNum("cos(60)", in), 0.5), "cos derece");
    CHECK(near(evalNum("atan2(1,1)", in), 45.0), "atan2 derece");
    CHECK(near(evalNum("clamp(5,0,3)", in), 3.0), "clamp ust");
    CHECK(near(evalNum("clamp(-1,0,3)", in), 0.0), "clamp alt");
    CHECK(near(evalNum("if(1,10,20)", in), 10.0), "if dogru dal");
    CHECK(near(evalNum("if(0,10,20)", in), 20.0), "if yanlis dal");
    CHECK(near(evalNum("3>2", in), 1.0), "karsilastirma >");
    CHECK(near(evalNum("2>=3", in), 0.0), "karsilastirma >=");
    CHECK(near(evalNum("2==2", in), 1.0), "esitlik ==");
    CHECK(near(evalNum("1 and 0", in), 0.0), "and");
    CHECK(near(evalNum("1 or 0", in), 1.0), "or");
    CHECK(near(evalNum("not 0", in), 1.0), "not");
    CHECK(near(evalNum("main.h", in), 300.0), "main.h");
    CHECK(near(evalNum("main.b", in), 300.0), "main.b");
    CHECK(near(evalNum("main.tw", in), 11.0), "main.tw");
    CHECK(near(evalNum("main.tf", in), 19.0), "main.tf");
    CHECK(near(evalNum("main.length", in), 3000.0), "main.length");
    CHECK(near(evalNum("secondary_0.h", in), 300.0), "secondary_0.h");
    CHECK(near(evalNum("secondary_0.b", in), 150.0), "secondary_0.b");
    CHECK(near(evalNum("secondary_0.length", in), 6000.0), "secondary_0.length");
    // let zinciri: X -> (Y + 1), Y -> 5.
    {
        Recipe r;
        r.lets.push_back({"Y", "5"});
        r.lets.push_back({"X", "Y + 1"});
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("X"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {f, p};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(plan.ok && !plan.plates.empty() && near(plan.plates[0].width, 6.0), "let zinciri");
    }
}

static void testParamAccess() {
    std::vector<CompParamSpec> specs;
    CompParamSpec s;
    s.name = L"plateThickness";
    s.type = CompParamType::Length;
    s.defDouble = 12.0;
    specs.push_back(s);
    ComponentInput in = makeInput();
    CompParam p; p.name = L"plateThickness"; p.value = CompParamValue{20.0};
    in.params.push_back(p);
    CHECK(near(evalNum("param.plateThickness", in, specs), 20.0), "param.* degeri");
}

static void testStaticAnalysis() {
    const ComponentInput in = makeInput();
    // bilinmeyen blok
    {
        Recipe r; RecipeStep s; s.id = "x"; s.use = "nope.bogus"; r.steps.push_back(s);
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "bilinmeyen blok reddedilir");
    }
    // bilinmeyen parametre
    {
        Recipe r; r.lets.push_back({"X", "param.olmayan"});
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("X"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {f, p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "bilinmeyen parametre reddedilir");
    }
    // tanimsiz let
    {
        Recipe r; r.lets.push_back({"X", "tanimsizLet"});
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("X"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {f, p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "tanimsiz let reddedilir");
    }
    // dongusel let
    {
        Recipe r; r.lets.push_back({"A", "B"}); r.lets.push_back({"B", "A"});
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "dongusel let reddedilir");
    }
    // yinelenen roleKey (ayni tur)
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p1; p1.id = "p1"; p1.use = "plate.rect"; p1.role = "end_plate";
        p1.args["frame"] = S("f"); p1.args["w"] = S("1"); p1.args["h"] = S("1"); p1.args["t"] = S("1");
        RecipeStep p2; p2.id = "p2"; p2.use = "plate.rect"; p2.role = "end_plate";
        p2.args["frame"] = S("f"); p2.args["w"] = S("1"); p2.args["h"] = S("1"); p2.args["t"] = S("1");
        r.steps = {f, p1, p2};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "yinelenen roleKey reddedilir");
    }
    // roleKey tur tutarsizligi (plate sonra bolt)
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "x";
        p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        RecipeStep b; b.id = "b"; b.use = "bolts.grid"; b.role = "x";
        b.args["frame"] = S("f"); b.args["through"] = L({"main"});
        r.steps = {f, p, b};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "roleKey farkli ture baglanamaz");
    }
    // rezerve role
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "main";
        p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {f, p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "rezerve roleKey reddedilir");
    }
    // adim siniri (>256)
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        r.steps.push_back(f);
        for (int i = 0; i < 260; ++i) {
            RecipeStep p; p.id = "p" + std::to_string(i); p.use = "plate.rect";
            p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
            r.steps.push_back(p);
        }
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "adim siniri 256");
    }
}

static void testBlocksProducePlan() {
    const ComponentInput in = makeInput();
    Recipe r;
    RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("secondary_0");
    RecipeStep pl; pl.id = "pl"; pl.use = "plate.rect"; pl.role = "end_plate";
    pl.args["frame"] = S("f"); pl.args["w"] = S("150"); pl.args["h"] = S("300");
    pl.args["t"] = S("20"); pl.args["material"] = S("S235JR");
    RecipeStep bl; bl.id = "bl"; bl.use = "bolts.grid"; bl.role = "beam_bolts";
    bl.args["frame"] = S("f"); bl.args["nx"] = S("2"); bl.args["ny"] = S("2");
    bl.args["px"] = S("60"); bl.args["py"] = S("90"); bl.args["d"] = S("20"); bl.args["hole"] = S("22");
    bl.args["through"] = L({"secondary_0"});
    RecipeStep w; w.id = "w"; w.use = "weld.fillet"; w.role = "tab_weld";
    w.args["roleA"] = S("main"); w.args["roleB"] = S("end_plate"); w.args["size"] = S("5");
    RecipeStep c; c.id = "c"; c.use = "cut.plane"; c.role = "beam_end_cut";
    c.args["target"] = S("secondary_0"); c.args["frame"] = S("f"); c.args["keep"] = S("positive");
    r.steps = {f, pl, bl, w, c};

    const GenerationPlan plan = runRecipe(r, {}, in);
    CHECK(plan.ok, "plan olusur");
    CHECK(plan.plates.size() == 1, "tek levha");
    CHECK(plan.boltGroups.size() == 1, "tek bulon grubu");
    CHECK(plan.welds.size() == 1, "tek kaynak");
    CHECK(plan.cuts.size() == 1, "tek kesim");
    if (plan.plates.size() == 1) {
        CHECK(near(plan.plates[0].width, 150.0), "levha genisligi");
        CHECK(near(plan.plates[0].thickness, 20.0), "levha kalinligi");
        CHECK(plan.plates[0].material == L"S235JR", "levha malzemesi");
    }
    if (plan.boltGroups.size() == 1) {
        CHECK(plan.boltGroups[0].countX == 2 && plan.boltGroups[0].countY == 2, "bulon 2x2");
        CHECK(plan.boltGroups[0].parts.size() == 1 && plan.boltGroups[0].parts[0] == 2,
              "bulon secondary_0'yi deler");
        CHECK(near(plan.boltGroups[0].params.nominalDiameter, 20.0), "bulon capi");
    }
    if (plan.welds.size() == 1) {
        CHECK(plan.welds[0].partA == 1, "kaynak partA = main");
        CHECK(plan.welds[0].partBRole == "end_plate", "kaynak partB = levha roleKey");
    }
    if (plan.cuts.size() == 1) {
        CHECK(plan.cuts[0].target == 2, "kesim hedefi secondary_0");
        CHECK(plan.cuts[0].kind == CutKind::PlaneCut, "kesim turu PlaneCut");
        CHECK(plan.cuts[0].hidden, "component kesimi gizli");
    }
    // roleKey'ler
    CHECK(plan.plateKeys.size() == 1 && plan.plateKeys[0] == "end_plate", "plate roleKey");
    CHECK(plan.boltGroupKeys.size() == 1 && plan.boltGroupKeys[0] == "beam_bolts", "bolt roleKey");
    CHECK(plan.weldKeys.size() == 1 && plan.weldKeys[0] == "tab_weld", "weld roleKey");
    CHECK(plan.cutKeys.size() == 1 && plan.cutKeys[0] == "beam_end_cut", "cut roleKey");
}

static void testCheckFailClosed() {
    const ComponentInput in = makeInput();
    Recipe r;
    RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
    RecipeStep p; p.id = "p"; p.use = "plate.rect";
    p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
    RecipeStep ck; ck.check = "1 > 2"; ck.message = "her zaman yanlis";
    r.steps = {f, p, ck};
    const GenerationPlan plan = runRecipe(r, {}, in);
    CHECK(!plan.ok, "check basarisiz -> plan ok=false");
    CHECK(plan.error == L"her zaman yanlis", "check mesaji hataya yazilir");
}

static void testFrameOffset() {
    const ComponentInput in = makeInput();
    Recipe r;
    RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
    RecipeStep fo; fo.id = "fo"; fo.use = "frame.offset";
    fo.args["frame"] = S("f"); fo.args["dw"] = S("100");
    RecipeStep p; p.id = "p"; p.use = "plate.rect";
    p.args["frame"] = S("fo"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
    r.steps = {f, fo, p};
    const GenerationPlan plan = runRecipe(r, {}, in);
    CHECK(plan.ok && plan.plates.size() == 1, "offset cerceve plan uretir");
    if (plan.ok && plan.plates.size() == 1) {
        // main uc cercevesi: origin axisTo = (0,0,3000), w = +Z, dw=100 -> z=3100.
        CHECK(near(plan.plates[0].center.z, 3100.0), "dw offset ana eksen boyunca");
    }
}

static void testWhenSkips() {
    const ComponentInput in = makeInput();
    Recipe r;
    RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
    RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.when = "0";
    p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
    r.steps = {f, p};
    const GenerationPlan plan = runRecipe(r, {}, in);
    CHECK(plan.ok, "when=0 atlanir, plan yine ok");
    CHECK(plan.plates.empty(), "when=0 -> levha uretilmez");
}

int main() {
    CHECK(std::string(kContractVersion) == "1.6.0", "contract surumu 1.6.0");
    testExpressionEvaluator();
    testParamAccess();
    testStaticAnalysis();
    testBlocksProducePlan();
    testCheckFailClosed();
    testFrameOffset();
    testWhenSkips();
    std::printf("recipe test: %s (%d hata)\n", failures ? "HATALI" : "OK", failures);
    return failures ? 1 : 0;
}
