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

int main() {
    CHECK(std::string(kContractVersion) == "1.10.0", "contract surumu 1.7.0");
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
    std::printf("recipe test: %s (%d hata)\n", failures ? "HATALI" : "OK", failures);
    return failures ? 1 : 0;
}
