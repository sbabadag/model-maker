// MM-3a — RECIPE YORUMLAYICI testi (OCCT/Qt'siz, Linux + Windows mingw).
// Kapsam: (1) ifade degerlendirici, (2) param.*/main.*/secondary_0.* erisimi,
// (3) statik analiz (bilinmeyen blok/parametre/let, dongusel let, roleKey
// tekilligi/tur tutarliligi/rezerve, adim siniri), (4) v0 taslarinin plan
// uretimi, (5) fail-closed check, (6) frame.offset geometrisi.
#include "model_maker/recipe.hpp"
#include "model_maker/component_codec.hpp"
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
    // H: tanimsiz cerceve basvurusu (plate.rect frame -> onceden tanimli id degil).
    {
        Recipe r;
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("zz"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "H: tanimsiz cerceve reddedilir");
        CHECK(v.message.find(L"tanimsiz cerceve") != std::wstring::npos,
              "H: mesaj 'tanimsiz cerceve' der");
        CHECK(v.message.find(L"adim 0") != std::wstring::npos,
              "H: mesaj adim indeksini soyler");
        if (!v.ok) std::printf("   H mesaj: %ls\n", v.message.c_str());
    }
    // H: frame.offset'in giris cercevesi de tanimli olmali (hem tuketir hem uretir).
    {
        Recipe r;
        RecipeStep o; o.id = "o"; o.use = "frame.offset"; o.args["frame"] = S("zz");
        r.steps = {o};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "H: frame.offset tanimsiz giris cercevesi reddedilir");
        CHECK(v.message.find(L"tanimsiz cerceve") != std::wstring::npos,
              "H: mesaj 'tanimsiz cerceve' der");
    }
    // H: ileri referans (henuz uretilmemis adimin id'si) reddedilir.
    {
        Recipe r;
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        r.steps = {p, f};   // p, f'den ONCE -> tanimsiz
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "H: ileri frame referansi reddedilir");
        CHECK(v.message.find(L"adim 0") != std::wstring::npos,
              "H: ileri referans mesaji adim indeksini soyler");
    }
    // H: gecerli frame zinciri (once uret, sonra kullan) kabul edilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep o; o.id = "o"; o.use = "frame.offset"; o.args["frame"] = S("f");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("o"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {f, o, p};
        const auto v = validateRecipe(r, {});
        CHECK(v.ok, "H: gecerli frame zinciri kabul edilir");
    }
    // O (migration): frame.contact eski adlari YENI adi soyleyerek reddedilir.
    {
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.contact"; c.args["main"] = S("main");
        c.args["secondary"] = S("secondary_0");
        r.steps = {c};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "O: frame.contact eski adi reddedilir");
        CHECK(v.message.find(L"yeniden adlandirildi") != std::wstring::npos,
              "O: mesaj yeniden adlandirmayi soyler");
        if (!v.ok) std::printf("   O mesaj: %ls\n", v.message.c_str());
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
        CHECK(plan.boltGroups[0].partRoles.size() == 1 &&
                  plan.boltGroups[0].partRoles[0] == "secondary_0",
              "bulon partRoles = secondary_0 (MM-2b)");
        CHECK(near(plan.boltGroups[0].params.nominalDiameter, 20.0), "bulon capi");
    }
    if (plan.welds.size() == 1) {
        CHECK(plan.welds[0].partA == 1, "kaynak partA = main");
        CHECK(plan.welds[0].partARole == "main", "kaynak partARole = main (MM-2b)");
        CHECK(plan.welds[0].partBRole == "end_plate", "kaynak partB = levha roleKey");
    }
    if (plan.cuts.size() == 1) {
        CHECK(plan.cuts[0].target == 2, "kesim hedefi secondary_0");
        CHECK(plan.cuts[0].targetRole == "secondary_0", "kesim targetRole = secondary_0 (MM-2b)");
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

// ST-4 (1): plate.rect'in STRING alanlari (name/material/finish/posNo) `param.<ad>`
// kabul eder. Eskiden strArg ile okunuyordu: recipe'de "param.plateMaterial"
// yazildiysa levha malzemesi literal "param.plateMaterial" oluyordu.
static void testPlateStringParams() {
    ComponentInput in = makeInput();
    in.params.push_back(CompParam{L"plateMaterial", CompParamValue{std::wstring(L"S355JR")}});
    in.params.push_back(CompParam{L"plateName", CompParamValue{std::wstring(L"Taban levhasi")}});
    in.params.push_back(CompParam{L"plateFinish", CompParamValue{std::wstring(L"Galvaniz")}});
    in.params.push_back(CompParam{L"posNo", CompParamValue{std::wstring(L"P-101")}});

    // Parametre SEMASI: strParamArg kullanan metin argumanlari icin parametreler
    // semada BULUNMAK ZORUNDA (HR-7 statik denetimi). Bu, paketlerin gercek
    // kullanimidir (parameters.json -> specs).
    const auto textSpec = [](const wchar_t* name, const wchar_t* def) {
        CompParamSpec s;
        s.name = name;
        s.type = CompParamType::String;
        s.defString = def;
        return s;
    };
    std::vector<CompParamSpec> specs{textSpec(L"plateMaterial", L"S235JR"),
                                     textSpec(L"plateName", L""),
                                     textSpec(L"plateFinish", L""),
                                     textSpec(L"posNo", L"")};

    const auto runPlate = [&specs](const ComponentInput& input, const std::string& name,
                                   const std::string& material, const std::string& finish,
                                   const std::string& posNo) {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "base_plate";
        p.args["frame"] = S("f");
        p.args["w"] = S("100"); p.args["h"] = S("200"); p.args["t"] = S("12");
        if (!name.empty()) p.args["name"] = S(name);
        if (!material.empty()) p.args["material"] = S(material);
        if (!finish.empty()) p.args["finish"] = S(finish);
        if (!posNo.empty()) p.args["posNo"] = S(posNo);
        r.steps = {f, p};
        return runRecipe(r, specs, input);
    };

    // 1) param.<ad> -> parametre degeri (literal metin DEGIL).
    {
        const GenerationPlan plan = runPlate(in, "param.plateName", "param.plateMaterial",
                                             "param.plateFinish", "param.posNo");
        CHECK(plan.ok, "plate.rect param.* : plan ok");
        if (!plan.ok) std::printf("   hata: %ls\n", plan.error.c_str());
        CHECK(!plan.plates.empty(), "plate.rect param.* : levha uretildi");
        if (!plan.plates.empty()) {
            CHECK(plan.plates[0].name == L"Taban levhasi", "plate.rect name = param.plateName");
            CHECK(plan.plates[0].material == L"S355JR", "plate.rect material = param.plateMaterial");
            CHECK(plan.plates[0].finish == L"Galvaniz", "plate.rect finish = param.plateFinish");
            CHECK(plan.plates[0].posNo == L"P-101", "plate.rect posNo = param.posNo");
            CHECK(plan.plates[0].material != L"param.plateMaterial",
                  "plate.rect material literal 'param.plateMaterial' DEGIL");
        }
    }

    // 2) Literal metin eskisi gibi aynen gecer (geriye donuk uyum).
    {
        const GenerationPlan plan = runPlate(in, "", "S235JR", "", "P-2");
        CHECK(plan.ok, "plate.rect literal: plan ok");
        if (!plan.plates.empty()) {
            CHECK(plan.plates[0].material == L"S235JR", "plate.rect literal material aynen");
            CHECK(plan.plates[0].posNo == L"P-2", "plate.rect literal posNo aynen");
        }
    }

    // 3) Girdi parametresi yoksa SEMA varsayilani kullanilir.
    {
        ComponentInput emptyParams = in;
        emptyParams.params.clear();
        const GenerationPlan plan = runPlate(emptyParams, "", "param.plateMaterial", "", "");
        CHECK(plan.ok, "plate.rect sema varsayilani: plan ok");
        if (!plan.plates.empty())
            CHECK(plan.plates[0].material == L"S235JR", "plate.rect material = sema varsayilani");
    }

    // 4) Tanimsiz metin parametresi -> FAIL-CLOSED + DOLU mesaj (HR-7).
    {
        const GenerationPlan plan = runPlate(in, "param.yokBoyleParametre", "", "", "");
        CHECK(!plan.ok, "plate.rect tanimsiz param -> plan.ok=false");
        CHECK(!plan.error.empty(), "plate.rect tanimsiz param -> Turkce mesaj DOLU");
    }
}

// ST-4 (2): frame.member_end, uyenin kesit cercevesini profileRotation ile kurar
// (memberSectionFrame). Donuk kolonda genislik <-> yukseklik yer degistirir;
// eski kod profileSectionFrame kullanip donusu YOK SAYIYORDU.
static void testFrameRotation() {
    const auto plateOf = [](double rotationDeg) {
        ComponentInput in = makeInput();
        in.main.profileRotation = rotationDeg;
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "base_plate";
        p.args["frame"] = S("f");
        p.args["w"] = S("100"); p.args["h"] = S("200"); p.args["t"] = S("12");
        r.steps = {f, p};
        return runRecipe(r, {}, in);
    };

    const GenerationPlan flat = plateOf(0.0);
    const GenerationPlan rot90 = plateOf(90.0);
    CHECK(flat.ok && rot90.ok, "donuk uye: iki plan da ok");
    CHECK(!flat.plates.empty() && !rot90.plates.empty(), "donuk uye: levha uretildi");
    if (flat.plates.empty() || rot90.plates.empty()) return;

    // 90°: u' = v, v' = -u (rotateSectionFrame semantigi).
    const PlateSpec& a = flat.plates[0];
    const PlateSpec& b = rot90.plates[0];
    const auto same = [](const Vec3& x, const Vec3& y, double sign) {
        return near(x.x, y.x * sign) && near(x.y, y.y * sign) && near(x.z, y.z * sign);
    };
    CHECK(same(b.u, a.v, 1.0), "90°: genislik yonu eski yukseklik yonune dondu");
    CHECK(same(b.v, a.u, -1.0), "90°: yukseklik yonu eski genislik yonunun tersi");
    CHECK(!same(b.u, a.u, 1.0), "90°: cerceve gercekten dondu (u ayni kalmadi)");
    // Olcu yer degistirmez: w=100 h=200 her iki planda da ayni (donus yalniz yon).
    CHECK(near(a.width, 100.0) && near(b.width, 100.0), "donus: width degismez");
    CHECK(near(a.height, 200.0) && near(b.height, 200.0), "donus: height degismez");
    // Eksen (w) ayni kalmali.
    CHECK(same(a.axis, b.axis, 1.0), "donus: axis (kalinlik yonu) degismez");
}

// ST-4 (HR-1/3/4): frame.rotate, plate.rect pah, plate.polygon, ankraj grip/holeDepth.
static void testSt4Stones() {
    ComponentInput in = makeInput();

    // --- HR-1 frame.rotate ---------------------------------------------------
    const auto plateFrame = [&in](const std::string& use, const std::string& about,
                                  const std::string& deg, int extraRotations = 0) {
        Recipe r;
        RecipeStep base; base.id = "base"; base.use = "frame.member_end";
        base.args["member"] = S("main");
        r.steps.push_back(base);
        std::string prev = "base";
        int idx = 0;
        const int rotations = extraRotations > 0 ? extraRotations : 1;
        for (int i = 0; i < rotations; ++i) {
            RecipeStep rot;
            rot.id = "rot" + std::to_string(idx++);
            rot.use = use;
            rot.args["frame"] = S(prev);
            if (!about.empty()) rot.args["about"] = S(about);
            if (!deg.empty()) rot.args["deg"] = S(deg);
            r.steps.push_back(rot);
            prev = rot.id;
        }
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "base_plate";
        p.args["frame"] = S(prev);
        p.args["w"] = S("100"); p.args["h"] = S("200"); p.args["t"] = S("12");
        r.steps.push_back(p);
        return runRecipe(r, {}, in);
    };
    {
        const GenerationPlan flat = plateFrame("frame.rotate", "w", "0");
        const GenerationPlan plain = plateFrame("frame.rotate", "w", "0", 0);
        CHECK(flat.ok, "frame.rotate 0°: plan ok");
        CHECK(!flat.plates.empty(), "frame.rotate 0°: levha uretildi");
        CHECK(generationPlanDigest(flat) == generationPlanDigest(plain),
              "frame.rotate 0°: kimlik donusu (digest ayni)");
    }
    {
        const GenerationPlan r90 = plateFrame("frame.rotate", "w", "90");
        CHECK(r90.ok, "frame.rotate 90° w: plan ok");
        if (!r90.plates.empty() && !plateFrame("frame.rotate", "w", "0").plates.empty()) {
            const PlateSpec& a = plateFrame("frame.rotate", "w", "0").plates[0];
            const PlateSpec& b = r90.plates[0];
            const auto near3 = [](const Vec3& x, const Vec3& y, double s = 1.0) {
                return near(x.x, y.x * s, 1e-9) && near(x.y, y.y * s, 1e-9) &&
                       near(x.z, y.z * s, 1e-9);
            };
            CHECK(near3(b.u, a.v), "frame.rotate 90° w: u' = v");
            CHECK(near3(b.v, a.u, -1.0), "frame.rotate 90° w: v' = -u");
            CHECK(near3(b.axis, a.axis), "frame.rotate 90° w: w (normal) degismez");
        }
    }
    {
        // 4 x 90° = 360° -> baslangica doner (1e-9).
        const GenerationPlan rot4 = plateFrame("frame.rotate", "u", "90", 4);
        const GenerationPlan flat = plateFrame("frame.rotate", "u", "0");
        CHECK(rot4.ok && flat.ok, "frame.rotate 4x90° u: iki plan da ok");
        if (!rot4.plates.empty() && !flat.plates.empty()) {
            const PlateSpec& a = flat.plates[0];
            const PlateSpec& b = rot4.plates[0];
            const auto near3 = [](const Vec3& x, const Vec3& y) {
                return near(x.x, y.x, 1e-9) && near(x.y, y.y, 1e-9) && near(x.z, y.z, 1e-9);
            };
            CHECK(near3(a.u, b.u) && near3(a.v, b.v) && near3(a.axis, b.axis),
                  "frame.rotate 4x90° u: kimlige doner (1e-9)");
        }
    }
    {
        // Ortonormal + el yonu korunur: cross(u,v) hala w (ya da -w) ile ayni yonde.
        const GenerationPlan r = plateFrame("frame.rotate", "w", "37");
        CHECK(r.ok, "frame.rotate 37°: plan ok");
        if (!r.plates.empty()) {
            const PlateSpec& p = r.plates[0];
            const double dotUV = p.u.x * p.v.x + p.u.y * p.v.y + p.u.z * p.v.z;
            const double lenU = std::sqrt(p.u.x * p.u.x + p.u.y * p.u.y + p.u.z * p.u.z);
            CHECK(std::fabs(dotUV) < 1e-9, "frame.rotate: u/v dik (ortonormal)");
            CHECK(std::fabs(lenU - 1.0) < 1e-9, "frame.rotate: u birim");
        }
    }
    {
        Recipe r;
        RecipeStep base; base.id = "base"; base.use = "frame.member_end";
        base.args["member"] = S("main");
        RecipeStep bad; bad.id = "bad"; bad.use = "frame.rotate";
        bad.args["frame"] = S("base"); bad.args["about"] = S("x"); bad.args["deg"] = S("90");
        r.steps = {base, bad};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(!plan.ok && !plan.error.empty(), "frame.rotate: about 'x' -> fail-closed + mesaj");
    }

    // --- HR-3 plate.rect pah -------------------------------------------------
    const auto chamferPlan = [&in](std::vector<std::string> chamfers) {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect"; p.role = "base_plate";
        p.args["frame"] = S("f");
        p.args["w"] = S("400"); p.args["h"] = S("400"); p.args["t"] = S("20");
        if (!chamfers.empty()) p.args["chamfers"] = L(chamfers);
        r.steps = {f, p};
        return runRecipe(r, {}, in);
    };
    {
        const GenerationPlan plan = chamferPlan({"2:1:30:40"});
        CHECK(plan.ok, "plate.rect pah: plan ok");
        if (!plan.plates.empty()) {
            CHECK(plan.plates[0].chamfers.size() == 1, "plate.rect pah: 1 pah");
            if (plan.plates[0].chamfers.size() == 1) {
                const auto& ch = plan.plates[0].chamfers[0];
                CHECK(ch.corner == 2 && ch.type == 1, "plate.rect pah: corner 2, type 1");
                CHECK(near(ch.h, 30.0) && near(ch.v, 40.0), "plate.rect pah: h/v bacaklari");
            }
        }
    }
    {
        const GenerationPlan plan = chamferPlan({"1:1:20+20:25"});  // ifade: h = 40
        CHECK(plan.ok, "plate.rect pah: ifade bacagi plan ok");
        if (!plan.plates.empty() && plan.plates[0].chamfers.size() == 1)
            CHECK(near(plan.plates[0].chamfers[0].h, 40.0),
                  "plate.rect pah: ifade bacagi degerlendirildi (20+20=40)");
    }
    {
        const GenerationPlan plan = chamferPlan({"2:1:500:40"});  // w=400 -> sigmaz
        CHECK(!plan.ok, "plate.rect pah: kenari asan bacak REDDEDILIR");
        CHECK(plan.error.find(L"asiyor") != std::wstring::npos,
              "plate.rect pah: mesaj 'asiyor' icerir (kirpma yok)");
    }
    {
        const GenerationPlan plan = chamferPlan({"0:1:250:20", "1:1:250:20"});  // 500 > 400
        CHECK(!plan.ok, "plate.rect pah: komsu pahlar cakisirsa REDDEDILIR");
    }
    {
        const GenerationPlan plan = chamferPlan({"2:1:30:40", "2:1:20:20"});
        CHECK(!plan.ok, "plate.rect pah: ayni koseye iki pah REDDEDILIR");
    }
    {
        const GenerationPlan plan = chamferPlan({"2-1-30-40"});
        CHECK(!plan.ok, "plate.rect pah: bozuk bicim REDDEDILIR");
    }

    // --- HR-3 plate.polygon --------------------------------------------------
    const auto polygonPlan = [&in](std::vector<std::string> pts, const std::string& t = "12") {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.polygon"; p.role = "stiffener";
        p.args["frame"] = S("f"); p.args["t"] = S(t);
        p.args["outline"] = L(pts);
        r.steps = {f, p};
        return runRecipe(r, {}, in);
    };
    {
        const GenerationPlan plan = polygonPlan({"0,0", "main.b,0", "0,main.h"});
        CHECK(plan.ok, "plate.polygon: ucgen plan ok");
        if (!plan.plates.empty()) {
            const auto& pl = plan.plates[0];
            CHECK(pl.outline.size() == 3, "plate.polygon: 3 nokta");
            CHECK(near(pl.outline[1].first, 300.0) && near(pl.outline[2].second, 300.0),
                  "plate.polygon: ifade noktalari cozuldu");
            CHECK(near(pl.thickness, 12.0), "plate.polygon: kalinlik");
            // P-2: kontur dolu ise width/height SINIR KUTUSUDUR. 0 kalirsa
            // ComponentMaterializer::buildPlateSolid ilk kapida reddeder ve
            // `plate.polygon` levhasi hic uretilemez ("levha kati uretilemedi").
            CHECK(near(pl.width, 300.0) && near(pl.height, 300.0),
                  "plate.polygon: width/height konturun sinir kutusu");
        }
    }
    {
        const GenerationPlan plan = polygonPlan({"0,0", "100,0"});
        CHECK(!plan.ok, "plate.polygon: 2 nokta REDDEDILIR");
        CHECK(plan.error.find(L"en az 3") != std::wstring::npos,
              "plate.polygon: 'en az 3' mesaji");
    }
    {
        // Icbukey (L bicimi) -> RED.
        const GenerationPlan plan = polygonPlan({"0,0", "100,0", "100,40", "40,40", "40,100", "0,100"});
        CHECK(!plan.ok, "plate.polygon: icbukey kontur REDDEDILIR");
    }
    {
        // Kendini kesen (papyon) -> RED.
        const GenerationPlan plan = polygonPlan({"0,0", "100,100", "100,0", "0,100"});
        CHECK(!plan.ok, "plate.polygon: kendini kesen kontur REDDEDILIR");
    }
    {
        const GenerationPlan plan = polygonPlan({"0,0", "100,0"}, "0");
        CHECK(!plan.ok, "plate.polygon: kalinlik 0 REDDEDILIR");
    }

    // --- HR-4 bolts.grid grip / holeDepth -----------------------------------
    const auto boltPlan = [&in](const char* grip, const char* depth) {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep b; b.id = "b"; b.use = "bolts.grid"; b.role = "anchors";
        b.args["frame"] = S("f");
        b.args["nx"] = S("2"); b.args["ny"] = S("2");
        b.args["px"] = S("150"); b.args["py"] = S("150");
        if (grip) b.args["grip"] = S(grip);
        if (depth) b.args["holeDepth"] = S(depth);
        r.steps = {f, b};
        return runRecipe(r, {}, in);
    };
    {
        const GenerationPlan plan = boltPlan("300", "120");
        CHECK(plan.ok && plan.boltGroups.size() == 1, "bolts.grid grip: plan ok");
        if (!plan.boltGroups.empty()) {
            CHECK(near(plan.boltGroups[0].gripLength, 300.0), "bolts.grid grip: gripLength plana girdi");
            CHECK(near(plan.boltGroups[0].holeDepth, 120.0), "bolts.grid grip: holeDepth plana girdi");
        }
        const GenerationPlan plain = boltPlan(nullptr, nullptr);
        CHECK(plain.ok && !plain.boltGroups.empty(), "bolts.grid grip: argumansiz plan ok");
        if (!plain.boltGroups.empty()) {
            CHECK(near(plain.boltGroups[0].gripLength, 0.0) && near(plain.boltGroups[0].holeDepth, 0.0),
                  "bolts.grid grip: argumansiz -> 0 (mevcut davranis)");
        }
        CHECK(generationPlanDigest(plan) != generationPlanDigest(plain),
              "bolts.grid grip: grip/holeDepth digest'e girer");
    }
    {
        const GenerationPlan plan = boltPlan("-5", nullptr);
        CHECK(!plan.ok, "bolts.grid grip: negatif REDDEDILIR");
    }

    // --- I: bolts.grid VARSAYILAN grip (through uyelerinden tf/tw) -----------
    {
        const ComponentInput bi = makeInput();  // main tf=19, tw=11; beam tf=10.7, tw=7.1
        const auto defaultGrip = [&bi](const std::string& member, const char* dblGrip) {
            Recipe r;
            RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S(member);
            RecipeStep b; b.id = "b"; b.use = "bolts.grid";
            b.args["frame"] = S("f");
            b.args["nx"] = S("1"); b.args["ny"] = S("1");
            b.args["d"] = S("20"); b.args["hole"] = S("22");
            b.args["through"] = L({member});
            if (dblGrip) b.args["grip"] = S(dblGrip);
            r.steps = {f, b};
            return runRecipe(r, {}, bi);
        };
        // main: bolon ekseni uye eksenine paralel (frame.member_end w = uye ekseni)
        // -> aU/aV ikisi de ~0; tie-break aV>=aU secilir (tf).
        // Kisit: gercek dunyada bolon ekseni uyeye DIK olur (contact frame);
        // burada frame.member_end bolon eksenini uye ekseni yapar, bu yuzden
        // izdusumlar 0'dir. Yine de sonuc deterministik ve pozitif olmali.
        {
            const GenerationPlan p = defaultGrip("main", nullptr);
            CHECK(p.ok && p.boltGroups.size() == 1, "I: varsayilan grip plan ok");
            if (!p.boltGroups.empty()) {
                CHECK(p.boltGroups[0].gripLength > 0.0,
                      "I: varsayilan grip turetilir (>0)");
                CHECK(near(p.boltGroups[0].holeDepth, p.boltGroups[0].gripLength + 2.0),
                      "I: varsayilan holeDepth = grip + 2");
            }
        }
        // Acik grip verilirse dokunulmaz, holeDepth yine grip+2.
        {
            const GenerationPlan p = defaultGrip("main", "300");
            CHECK(p.ok && !p.boltGroups.empty(), "I: acik grip plan ok");
            if (!p.boltGroups.empty()) {
                CHECK(near(p.boltGroups[0].gripLength, 300.0), "I: acik grip korunur");
                CHECK(near(p.boltGroups[0].holeDepth, 302.0), "I: acik grip -> holeDepth=302");
            }
        }
        // Acik grip + acik holeDepth: ikisi de korunur.
        {
            Recipe r;
            RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
            RecipeStep b; b.id = "b"; b.use = "bolts.grid";
            b.args["frame"] = S("f");
            b.args["nx"] = S("1"); b.args["ny"] = S("1");
            b.args["through"] = L({"main"});
            b.args["grip"] = S("300");
            b.args["holeDepth"] = S("120");
            r.steps = {f, b};
            const GenerationPlan p = runRecipe(r, {}, bi);
            CHECK(p.ok && !p.boltGroups.empty(), "I: acik grip+holeDepth plan ok");
            if (!p.boltGroups.empty()) {
                CHECK(near(p.boltGroups[0].gripLength, 300.0) &&
                      near(p.boltGroups[0].holeDepth, 120.0),
                      "I: acik grip+holeDepth korunur");
            }
        }
    }

    // --- HR-7 statik denetim -------------------------------------------------
    {
        CompParamSpec g;
        g.name = L"grade";
        g.type = CompParamType::String;
        g.defString = L"8.8";
        CompParamSpec len;
        len.name = L"boltDia";
        len.type = CompParamType::Length;
        len.defDouble = 20.0;
        std::vector<CompParamSpec> specs{g, len};
        const auto validate = [&](const char* gradeArg) {
            Recipe r;
            RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
            RecipeStep b; b.id = "b"; b.use = "bolts.grid"; b.role = "grp";
            b.args["frame"] = S("f"); b.args["grade"] = S(gradeArg);
            r.steps = {f, b};
            return validateRecipe(r, specs);
        };
        const RecipeValidation unknown = validate("param.yok");
        CHECK(!unknown.ok, "HR-7: bilinmeyen metin parametresi validate'te REDDEDILIR");
        CHECK(unknown.message.find(L"bilinmeyen parametre") != std::wstring::npos,
              "HR-7: 'bilinmeyen parametre' mesaji");
        const RecipeValidation notText = validate("param.boltDia");
        CHECK(!notText.ok, "HR-7: metin olmayan parametre REDDEDILIR");
        CHECK(notText.message.find(L"metin parametresi degil") != std::wstring::npos,
              "HR-7: 'metin parametresi degil' mesaji");
        CHECK(validate("param.grade").ok, "HR-7: semadaki metin parametresi kabul");
        CHECK(validate("8.8").ok, "HR-7: literal metin kabul (geriye uyum)");
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

// J: bildirilmemis arguman fail-closed — 20 tasin her biri icin bir bilinmeyen.
static void testUnknownArgRejected() {
    const char* blocks[] = {
        "frame.member_end", "frame.offset", "frame.rotate", "frame.member_face",
        "plate.rect", "plate.polygon", "bolts.grid", "weld.fillet", "cut.plane",
        "endplate.placement", "frame.contact", "frame.plumb_face",
        "haunch.end_plate", "haunch.web", "haunch.flange",
        "haunch.stiffeners", "bolts.haunch_groups", "haunch.welds",
        "frame.bisector", "member.fit",
    };
    int n = 0;
    for (const char* use : blocks) {
        Recipe r;
        RecipeStep s;
        s.id = "x";
        s.use = use;
        // Tasin bildirmedigi bir arguman (or. 'origin' yazim hatasi).
        s.args["origin"] = S("1000; 2000; 0");
        r.steps = {s};
        const auto v = validateRecipe(r, {});
        const bool rejected = !v.ok &&
            v.message.find(L"bilinmeyen arguman") != std::wstring::npos &&
            v.message.find(wideFromUtf8(use)) != std::wstring::npos;
        if (!rejected) {
            std::printf("FAIL: J bilinmeyen arguman reddedilmedi: %s (%ls)\n",
                        use, v.message.c_str());
            ++failures;
        } else {
            ++n;
        }
    }
    std::printf("J: %d/%d tas bilinmeyen argumani reddetti\n", n, (int)(sizeof(blocks)/sizeof(blocks[0])));
}

// M1: weld.fillet double/doubleSided = param.* -> semada olmali ve Boolean.
static void testBoolParamArgStatic() {
    const auto hasBool = [](const std::string& name) {
        std::vector<CompParamSpec> specs;
        CompParamSpec b;
        b.name = wideFromUtf8(name);
        b.type = CompParamType::Boolean;
        b.defBool = false;
        specs.push_back(b);
        return specs;
    };
    const auto hasLen = []() {
        std::vector<CompParamSpec> specs;
        CompParamSpec l;
        l.name = L"plateThickness";
        l.type = CompParamType::Length;
        l.defDouble = 12.0;
        specs.push_back(l);
        return specs;
    };
    auto weld = [](const std::string& dbl) {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep w; w.id = "w"; w.use = "weld.fillet";
        w.args["roleA"] = S("main"); w.args["double"] = S(dbl);
        r.steps = {f, w};
        return r;
    };
    // Gecerli: Boolean param.
    {
        Recipe r = weld("param.createWelds");
        CHECK(validateRecipe(r, hasBool("createWelds")).ok,
              "M1: double=param.Boolean kabul edilir");
    }
    // Bilinmeyen param.
    {
        Recipe r = weld("param.olmayan");
        const auto v = validateRecipe(r, hasBool("createWelds"));
        CHECK(!v.ok, "M1: double=param.bilinmeyen reddedilir");
        CHECK(v.message.find(L"bilinmeyen parametre") != std::wstring::npos,
              "M1: mesaj 'bilinmeyen parametre' der");
    }
    // Boolean olmayan param (Length).
    {
        Recipe r = weld("param.plateThickness");
        const auto v = validateRecipe(r, hasLen());
        CHECK(!v.ok, "M1: double=param.Length reddedilir");
        CHECK(v.message.find(L"Boolean parametre degil") != std::wstring::npos,
              "M1: mesaj 'Boolean parametre degil' der");
    }
    // Literal true gecerli (tarama yok).
    {
        Recipe r = weld("true");
        CHECK(validateRecipe(r, {}).ok, "M1: double=true literal kabul edilir");
    }
}

// M2: uye argumanlari (member/target/through) girdi rolu olmali.
static void testMemberArgStatic() {
    auto frameMain = []() {
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        return f;
    };
    // member: 'secondary_2' gecerli.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("secondary_2");
        r.steps = {f};
        CHECK(validateRecipe(r, {}).ok, "M2: member=secondary_2 kabul edilir");
    }
    // member: keyfi metin reddedilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("beam_A");
        r.steps = {f};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "M2: member=beam_A reddedilir");
        CHECK(v.message.find(L"girdi rolu") != std::wstring::npos, "M2: mesaj 'girdi rolu' der");
    }
    // member: 'secondary_x' (rakam degil) reddedilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("secondary_x");
        r.steps = {f};
        CHECK(!validateRecipe(r, {}).ok, "M2: member=secondary_x reddedilir");
    }
    // bolts.grid through: gecersiz oge reddedilir.
    {
        Recipe r;
        RecipeStep f = frameMain();
        RecipeStep b; b.id = "b"; b.use = "bolts.grid";
        b.args["frame"] = S("f"); b.args["through"] = L({"main", "bogus"});
        r.steps = {f, b};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "M2: through gecersiz oge reddedilir");
        CHECK(v.message.find(L"girdi rolu") != std::wstring::npos, "M2: through mesaji 'girdi rolu' der");
    }
    // bolts.grid through: gecerli roller kabul edilir.
    {
        Recipe r;
        RecipeStep f = frameMain();
        RecipeStep b; b.id = "b"; b.use = "bolts.grid";
        b.args["frame"] = S("f"); b.args["through"] = L({"main", "secondary_0"});
        r.steps = {f, b};
        CHECK(validateRecipe(r, {}).ok, "M2: through gecerli roller kabul edilir");
    }
    // cut.plane target: gecersiz reddedilir.
    {
        Recipe r;
        RecipeStep f = frameMain();
        RecipeStep c; c.id = "c"; c.use = "cut.plane";
        c.args["target"] = S("bogus"); c.args["frame"] = S("f");
        r.steps = {f, c};
        CHECK(!validateRecipe(r, {}).ok, "M2: cut.plane target=bogus reddedilir");
    }
}

// M3: chamfers/outline liste ogeleri statik taranir (bilinmeyen param/let).
static void testExprListStatic() {
    // chamfers: '3:1:t*2:30' -> 't' tanimsiz let olur (let yoksa).
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("100"); p.args["h"] = S("200");
        p.args["t"] = S("10");
        p.args["chamfers"] = L({"3:1:param.olmayan:30"});
        r.steps = {f, p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "M3: chamfers bilinmeyen param reddedilir");
        CHECK(v.message.find(L"liste ogesi") != std::wstring::npos, "M3: mesaj 'liste ogesi' der");
    }
    // chamfers: gecerli ifadeler kabul edilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("f"); p.args["w"] = S("100"); p.args["h"] = S("200");
        p.args["t"] = S("10");
        p.args["chamfers"] = L({"0:1:20:20"});
        r.steps = {f, p};
        CHECK(validateRecipe(r, {}).ok, "M3: chamfers gecerli ifade kabul edilir");
    }
    // outline: 'param.olmayan,20' reddedilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.polygon";
        p.args["frame"] = S("f"); p.args["t"] = S("10");
        p.args["outline"] = L({"0,0", "param.olmayan,20", "0,40"});
        r.steps = {f, p};
        const auto v = validateRecipe(r, {});
        CHECK(!v.ok, "M3: outline bilinmeyen param reddedilir");
    }
    // outline: gecerli ifade kabul edilir.
    {
        Recipe r;
        RecipeStep f; f.id = "f"; f.use = "frame.member_end"; f.args["member"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.polygon";
        p.args["frame"] = S("f"); p.args["t"] = S("10");
        p.args["outline"] = L({"0,0", "100,0", "0,40"});
        r.steps = {f, p};
        CHECK(validateRecipe(r, {}).ok, "M3: outline gecerli ifade kabul edilir");
    }
}

// L: blockDeclarations() salt okunur API — 20 tas, tur ve sekil bilgisi.
static void testBlockDeclarations() {
    const std::vector<BlockDeclInfo> decls = blockDeclarations();
    CHECK(decls.size() == 20, "L: 20 tas bildirilir");
    const auto find = [&](const std::string& use) -> const BlockDeclInfo* {
        for (const auto& d : decls)
            if (d.use == use) return &d;
        return nullptr;
    };
    const auto argKind = [&](const std::string& use, const std::string& name,
                             RecipeArgKind& kind, bool& isList, bool& exprItems) -> bool {
        const BlockDeclInfo* d = find(use);
        if (!d) return false;
        for (const auto& a : d->args)
            if (a.name == name) { kind = a.kind; isList = a.isList; exprItems = a.exprItems; return true; }
        return false;
    };
    RecipeArgKind k; bool l = false, e = false;
    // frame.member_end: member = MemberRole (skaler).
    CHECK(argKind("frame.member_end", "member", k, l, e) && k == RecipeArgKind::MemberRole && !l,
          "L: frame.member_end.member = MemberRole skaler");
    // frame.member_face: member/toward = MemberRole (skaler).
    CHECK(argKind("frame.member_face", "member", k, l, e) && k == RecipeArgKind::MemberRole && !l,
          "L: frame.member_face.member = MemberRole skaler");
    CHECK(argKind("frame.member_face", "toward", k, l, e) && k == RecipeArgKind::MemberRole && !l,
          "L: frame.member_face.toward = MemberRole skaler");
    // plate.rect: w = Expression; chamfers = List + exprItems.
    CHECK(argKind("plate.rect", "w", k, l, e) && k == RecipeArgKind::Expression,
          "L: plate.rect.w = Expression");
    CHECK(argKind("plate.rect", "chamfers", k, l, e) && k == RecipeArgKind::List && l && e,
          "L: plate.rect.chamfers = List + exprItems");
    CHECK(argKind("plate.rect", "material", k, l, e) && k == RecipeArgKind::TextParam,
          "L: plate.rect.material = TextParam");
    // plate.polygon: outline = List + exprItems.
    CHECK(argKind("plate.polygon", "outline", k, l, e) && k == RecipeArgKind::List && l && e,
          "L: plate.polygon.outline = List + exprItems");
    // bolts.grid: through = MemberRole + isList.
    CHECK(argKind("bolts.grid", "through", k, l, e) && k == RecipeArgKind::MemberRole && l,
          "L: bolts.grid.through = MemberRole liste");
    // weld.fillet: double = Logical.
    CHECK(argKind("weld.fillet", "double", k, l, e) && k == RecipeArgKind::Logical,
          "L: weld.fillet.double = Logical");
    // cut.plane: target = MemberRole.
    CHECK(argKind("cut.plane", "target", k, l, e) && k == RecipeArgKind::MemberRole,
          "L: cut.plane.target = MemberRole");
    // endplate.placement: main/beam = Text (rol, girdi rolu DENETIMI M2'de degil).
    CHECK(argKind("endplate.placement", "beam", k, l, e) && k == RecipeArgKind::Text,
          "L: endplate.placement.beam = Text");
    // haunch.end_plate: haunchLength = Expression; plateMaterial = TextParam.
    CHECK(argKind("haunch.end_plate", "haunchLength", k, l, e) && k == RecipeArgKind::Expression,
          "L: haunch.end_plate.haunchLength = Expression");
    CHECK(argKind("haunch.end_plate", "plateMaterial", k, l, e) && k == RecipeArgKind::TextParam,
          "L: haunch.end_plate.plateMaterial = TextParam");
    // Argumanlar ada gore alfabetik sirali (deterministik ayna).
    const BlockDeclInfo* hg = find("bolts.haunch_groups");
    CHECK(hg != nullptr, "L: bolts.haunch_groups var");
    if (hg) {
        bool sorted = true;
        for (std::size_t i = 1; i < hg->args.size(); ++i)
            if (!(hg->args[i - 1].name < hg->args[i].name)) sorted = false;
        CHECK(sorted, "L: argumanlar alfabetik sirali");
    }
    std::printf("L: %zu tas bildirimi dogrulandi\n", decls.size());
}

// P: frame.member_face — baglantiya gore yonlenen yuz cercevesi.
// Kanonik girdiyle (memberGeometryFromProfile) kurulur; sahne Z'de 4 yon x kiris
// iki cizim yonu -> ayni recipe ayni FIZIKSEL yuze oturur. Kolon dusey (kesit
// cercevesi dunya-bagli: width=+X, height=+Y), kiris yatay.
static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-6) {
    return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol &&
           std::fabs(a.z - b.z) <= tol;
}

static void testMemberFace() {
    const MemberSectionSpec colSec{300.0, 300.0, 11.0, 19.0, 0.0};
    const MemberSectionSpec beamSec{150.0, 300.0, 7.1, 10.7, 0.0};
    const Vec3 colBase{0.0, 0.0, 0.0}, colTop{0.0, 0.0, 3000.0};

    // Cerceveyi uret ve plate.rect ile oku (1x1x1 levha -> center=origin,
    // u/v/axis = frame u/v/w).
    auto faceFrame = [&](const Vec3& beamDir, bool connAtA) -> MemberLocalFrame {
        const Vec3 farEnd = colTop + beamDir;
        ComponentInput in;
        in.main = memberGeometryFromProfile(colBase, colTop, /*connectionAtA=*/false,
                                            Vec3{0.0, 0.0, 1.0}, 0.0, colSec);
        in.main.id = 1;
        MemberGeometry beam = memberGeometryFromProfile(
            connAtA ? colTop : farEnd, connAtA ? farEnd : colTop, connAtA,
            Vec3{0.0, 0.0, 1.0}, 0.0, beamSec);
        beam.id = 2;
        in.secondary.push_back(beam);
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.member_face";
        c.args["member"] = S("main"); c.args["toward"] = S("secondary_0");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("c"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {c, p};
        const GenerationPlan plan = runRecipe(r, {}, in);
        MemberLocalFrame out;
        if (!plan.ok || plan.plates.empty()) { out.origin = Vec3{1e9, 1e9, 1e9}; return out; }
        out.origin = plan.plates[0].center;
        out.u = plan.plates[0].u;
        out.v = plan.plates[0].v;
        out.w = plan.plates[0].axis;
        return out;
    };

    // 4 yon (sahne Z donusu 0/90/180/270) x govde(±X)/flans(±Y).
    struct Case { Vec3 dir; Vec3 w; Vec3 origin; };
    const Case cases[] = {
        {{6000.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {5.5, 0.0, 3000.0}},        // +X govde
        {{-6000.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {-5.5, 0.0, 3000.0}},     // -X govde
        {{0.0, 6000.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 150.0, 3000.0}},      // +Y flans
        {{0.0, -6000.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, -150.0, 3000.0}},   // -Y flans
    };
    for (const auto& cs : cases) {
        for (const bool connAtA : {true, false}) {   // kiris iki cizim yonu
            const MemberLocalFrame f = faceFrame(cs.dir, connAtA);
            CHECK(near3(f.w, cs.w), "member_face: w toward'a bakar (yuz normali)");
            CHECK(near3(f.origin, cs.origin), "member_face: origin dogru yuz duzleminde");
            // w yonu eksenle dik olmali (yuz normali), u/v ortonormal-ish.
            const double wAxisDot = std::fabs(f.w.x * 0.0 + f.w.y * 0.0 + f.w.z * 1.0);
            CHECK(wAxisDot <= 1e-6, "member_face: yuz normali kolon eksenine dik");
        }
    }

    // Govde baglantisinda u = yukseklik (+Y), flans baglantisinda u = genislik (+X).
    {
        const MemberLocalFrame web = faceFrame({6000.0, 0.0, 0.0}, true);
        CHECK(near3(web.u, {0.0, 1.0, 0.0}), "member_face: govde u = yukseklik");
        const MemberLocalFrame fl = faceFrame({0.0, 6000.0, 0.0}, true);
        CHECK(near3(fl.u, {1.0, 0.0, 0.0}), "member_face: flans u = genislik");
    }

    // Fail-closed: member == toward.
    {
        ComponentInput in;
        in.main = memberGeometryFromProfile(colBase, colTop, false, Vec3{0,0,1}, 0.0, colSec);
        in.main.id = 1;
        in.secondary.push_back(in.main); in.secondary[0].id = 2;
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.member_face";
        c.args["member"] = S("main"); c.args["toward"] = S("main");
        r.steps = {c};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(!plan.ok, "member_face: member==toward reddedilir");
    }
    // Fail-closed: toward eksenle ayni dogrultuda (dusey kiris).
    {
        ComponentInput in;
        in.main = memberGeometryFromProfile(colBase, colTop, false, Vec3{0,0,1}, 0.0, colSec);
        in.main.id = 1;
        MemberGeometry beam = memberGeometryFromProfile(colTop, Vec3{0,0,6000}, true,
                                                        Vec3{0,0,1}, 0.0, beamSec);
        beam.id = 2;
        in.secondary.push_back(beam);
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.member_face";
        c.args["member"] = S("main"); c.args["toward"] = S("secondary_0");
        r.steps = {c};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(!plan.ok, "member_face: toward eksenle ayni dogrultuda reddedilir");
    }
    // Fail-closed: toward 45° (iki yuze esit).
    {
        ComponentInput in;
        in.main = memberGeometryFromProfile(colBase, colTop, false, Vec3{0,0,1}, 0.0, colSec);
        in.main.id = 1;
        MemberGeometry beam = memberGeometryFromProfile(colTop, colTop + Vec3{6000.0, 6000.0, 0.0},
                                                        true, Vec3{0,0,1}, 0.0, beamSec);
        beam.id = 2;
        in.secondary.push_back(beam);
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.member_face";
        c.args["member"] = S("main"); c.args["toward"] = S("secondary_0");
        r.steps = {c};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(!plan.ok, "member_face: toward 45° (iki yuze esit) reddedilir");
    }
}

// N: runRecipeFrames — generic cerceveler salt okunur disari verilir.
static void testRunRecipeFrames() {
    const ComponentInput in = makeInput();
    Recipe r;
    RecipeStep c; c.id = "c"; c.use = "frame.member_end";
    c.args["member"] = S("main"); c.args["end"] = S("end");
    RecipeStep o; o.id = "o"; o.use = "frame.offset";
    o.args["frame"] = S("c"); o.args["dw"] = S("10");
    r.steps = {c, o};

    const auto frames = runRecipeFrames(r, {}, in);
    CHECK(frames.size() == 2, "runRecipeFrames: 2 generic cerceve doner");
    const auto fc = frames.find("c");
    CHECK(fc != frames.end() && near3(fc->second.origin, {0.0, 0.0, 3000.0}),
          "runRecipeFrames: frame.member_end origin");
    if (fc != frames.end())
        CHECK(near3(fc->second.w, {0.0, 0.0, 1.0}), "runRecipeFrames: member_end w = eksen");
    const auto fo = frames.find("o");
    CHECK(fo != frames.end() && near3(fo->second.origin, {0.0, 0.0, 3010.0}),
          "runRecipeFrames: frame.offset origin kaydi");

    // runRecipe ile plan ayni kalir (digest degismez): ayni recipe + ayni girdi.
    const GenerationPlan plan = runRecipe(r, {}, in);
    CHECK(plan.ok, "runRecipeFrames: runRecipe hala ok (plan degismedi)");

    // Basarisiz recipe -> bos harita.
    Recipe bad;
    RecipeStep b; b.id = "x"; b.use = "frame.offset";
    b.args["frame"] = S("yok");   // tanimsiz cerceve -> statik ret
    bad.steps = {b};
    const auto emptyFrames = runRecipeFrames(bad, {}, in);
    CHECK(emptyFrames.empty(), "runRecipeFrames: basarisiz recipe -> bos harita");
}

// O: frame.contact generic yuz cercevesi uretir (genel taslarca kullanilabilir).
static void testFrameContactGeneric() {
    const MemberSectionSpec colSec{300.0, 300.0, 11.0, 19.0, 0.0};
    const MemberSectionSpec beamSec{150.0, 300.0, 7.1, 10.7, 0.0};
    const Vec3 colBase{0.0, 0.0, 0.0}, colTop{0.0, 0.0, 3000.0};
    ComponentInput in;
    in.main = memberGeometryFromProfile(colBase, colTop, false, Vec3{0, 0, 1}, 0.0, colSec);
    in.main.id = 1;
    MemberGeometry beam = memberGeometryFromProfile(colTop, colTop + Vec3{6000.0, 0.0, 0.0},
                                                    true, Vec3{0, 0, 1}, 0.0, beamSec);
    beam.id = 2;
    in.secondary.push_back(beam);

    auto contactFrames = [&](const ComponentInput& input) {
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.contact";
        c.args["member"] = S("secondary_0"); c.args["target"] = S("main");
        r.steps = {c};
        return runRecipeFrames(r, {}, input);
    };
    auto faceFrames = [&](const ComponentInput& input) {
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.member_face";
        c.args["member"] = S("main"); c.args["toward"] = S("secondary_0");
        r.steps = {c};
        return runRecipeFrames(r, {}, input);
    };

    // frame.contact generic cercevesi == frame.member_face(main, secondary_0).
    const auto cf = contactFrames(in);
    const auto ff = faceFrames(in);
    CHECK(cf.count("c") && ff.count("c"), "frame.contact: generic cerceve uretilir");
    if (cf.count("c") && ff.count("c")) {
        CHECK(near3(cf.at("c").origin, ff.at("c").origin), "frame.contact: origin == member_face");
        CHECK(near3(cf.at("c").w, ff.at("c").w), "frame.contact: w == member_face");
        CHECK(near3(cf.at("c").w, {1.0, 0.0, 0.0}), "frame.contact: kolon govdesi +X");
    }

    // Genel tas (plate.rect) frame.contact ciktisini `frame` olarak kullanir.
    {
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.contact";
        c.args["member"] = S("secondary_0"); c.args["target"] = S("main");
        RecipeStep p; p.id = "p"; p.use = "plate.rect";
        p.args["frame"] = S("c"); p.args["w"] = S("1"); p.args["h"] = S("1"); p.args["t"] = S("1");
        r.steps = {c, p};
        const GenerationPlan plan = runRecipe(r, {}, in);
        CHECK(plan.ok && plan.plates.size() == 1, "frame.contact: plate.rect genel tasi kullanir");
        if (plan.ok && !plan.plates.empty())
            CHECK(near3(plan.plates[0].center, {5.5, 0.0, 3000.0}),
                  "frame.contact: levha kolon yuzunde (govde +X, tw/2)");
    }

    // member=secondary_1 genellemesi: ikinci kiris -Y (flans yuzu).
    {
        ComponentInput in2 = in;
        MemberGeometry beam2 = memberGeometryFromProfile(colTop, colTop + Vec3{0.0, -6000.0, 0.0},
                                                         true, Vec3{0, 0, 1}, 0.0, beamSec);
        beam2.id = 3;
        in2.secondary.push_back(beam2);
        Recipe r;
        RecipeStep c; c.id = "c"; c.use = "frame.contact";
        c.args["member"] = S("secondary_1"); c.args["target"] = S("main");
        r.steps = {c};
        const auto f2 = runRecipeFrames(r, {}, in2);
        CHECK(f2.count("c"), "frame.contact: member=secondary_1 cozulur");
        if (f2.count("c"))
            CHECK(near3(f2.at("c").w, {0.0, -1.0, 0.0}),
                  "frame.contact: secondary_1 yuzu -Y (flans)");
    }
}

int main() {
    CHECK(std::string(kContractVersion) == "1.24.0", "contract surumu 1.24.0 (1.11.0 beklenirdi)");
    testExpressionEvaluator();
    testParamAccess();
    testStaticAnalysis();
    testBlocksProducePlan();
    testCheckFailClosed();
    testFrameOffset();
    testPlateStringParams();
    testFrameRotation();
    testSt4Stones();
    testWhenSkips();
    testUnknownArgRejected();
    testBoolParamArgStatic();
    testMemberArgStatic();
    testExprListStatic();
    testBlockDeclarations();
    testMemberFace();
    testRunRecipeFrames();
    testFrameContactGeneric();
    std::printf("recipe test: %s (%d hata)\n", failures ? "HATALI" : "OK", failures);
    return failures ? 1 : 0;
}
