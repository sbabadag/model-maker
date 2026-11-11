// ============================================================================
// CONTRACT ISTEKLERI (Studio a–k) — surum gunlugu ile birebir test edilir.
//
// a) manifest.json kanonik anahtar sirasi
// b) writePackageJsonFile: genis/UTF-8 yol + sonda tek LF
// c) validateRecipeJsonHeader PUBLIC
// d) frame.member_end {end: lower|upper} (dunya Z)
// e) ankraj bulonu: boy, gomme boyu, tip (bolts.grid argumanlari)
// f) member.fit {target, frame, mode}
// g) ayni roleKey, dislayici `when` kosullariyla
// h) frame.bisector {a, b}
// i) frame.member_face {member, face: ...}
// j) bolts.grid esit olmayan aralik (xs/ys, Tekla "70 2*80 70")
// k) weld.fillet: type/length/offset/around/site
// ============================================================================
#include "mm_contract/contract_version.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/recipe.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

static std::string readAll(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

// --- a) manifest kanonik anahtar sirasi --------------------------------------
static void sectionA() {
    std::printf("[a] manifest.json kanonik anahtar sirasi\n");
    const std::string p = std::string(MM_PACKAGES_DIR) + "/end_plate_pkg/manifest.json";
    const std::string txt = readAll(p);
    CHECK(!txt.empty(), "a: manifest okunamadi");
    // Ust duzey anahtarlar dosyada gorunme sirasina gore toplanir.
    std::vector<std::string> order;
    std::size_t depth = 0;
    for (std::size_t i = 0; i < txt.size(); ++i) {
        const char c = txt[i];
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { if (depth) --depth; }
        else if (c == '"' && depth == 1) {
            const std::size_t end = txt.find('"', i + 1);
            if (end == std::string::npos) break;
            // anahtar ancak string'den sonra ':' geliyorsa (bosluk atlanarak)
            std::size_t k2 = end + 1;
            while (k2 < txt.size() && (txt[k2] == ' ' || txt[k2] == '\t' || txt[k2] == '\n')) ++k2;
            if (k2 < txt.size() && txt[k2] == ':') order.push_back(txt.substr(i + 1, end - i - 1));
            i = end;
        }
    }
    CHECK(order.size() >= 5, "a: ust duzey anahtarlar bulunamadi");
    bool sorted = true;
    for (std::size_t k = 1; k < order.size(); ++k)
        if (order[k - 1] > order[k]) sorted = false;
    if (!sorted) {
        std::printf("      sira: ");
        for (const auto& k : order) std::printf("%s ", k.c_str());
        std::printf("\n");
    }
    CHECK(sorted, "a: manifest anahtarlari alfabetik DEGIL");
    CHECK(txt.back() == '\n' && txt[txt.size() - 2] != '\n', "a: manifest tek LF ile bitmiyor");
}

// --- b) writePackageJsonFile: genis/UTF-8 yol + sonda tek LF -----------------
static void sectionB() {
    std::printf("[b] writePackageJsonFile genis yol + tek LF\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / fs::path(u8"mm_sozlesme_turkce_\u015f\u011f\u00fc");
    std::error_code ec;
    fs::remove_all(dir, ec);
    CHECK(fs::create_directories(dir, ec), "b: gecici klasor olusturulamadi");
    const fs::path f = dir / "manifest.json";
    const std::u8string fu8 = f.u8string();
    const std::string fUtf8(fu8.begin(), fu8.end());
    // gecerli: LF-only, tek LF son
    CHECK(writePackageJsonFile(f, "{\"a\":1}\n"), "b: gecerli metin yazilmaliydi");
    CHECK(readAll(f) == "{\"a\":1}\n", "b: yazilan baytlar farkli");
    // RED: sonda LF yok
    CHECK(!writePackageJsonFile(f, "{\"a\":1}"), "b: LF'siz metin REDDEDILMELIYDI");
    // RED: cift LF
    CHECK(!writePackageJsonFile(f, "{\"a\":1}\n\n"), "b: cift LF REDDEDILMELIYDI");
    // RED: CR
    CHECK(!writePackageJsonFile(f, "{\"a\":1}\r\n"), "b: CR'li metin REDDEDILMELIYDI");
    // string (UTF-8) imzasi da ayni klasore yazabilmeli
    const fs::path g = dir / "dialog.json";
    const std::u8string gu8 = g.u8string();
    const std::string gUtf8(gu8.begin(), gu8.end());
    CHECK(writePackageJsonFile(gUtf8, "{\"b\":2}\n"), "b: UTF-8 string imzasi yazamadi");
    CHECK(readAll(g) == "{\"b\":2}\n", "b: UTF-8 string imzasi baytlari farkli");
    fs::remove_all(dir, ec);
}

// --- c) validateRecipeJsonHeader PUBLIC --------------------------------------
static void sectionC() {
    std::printf("[c] validateRecipeJsonHeader PUBLIC\n");
    std::wstring err;
    nlohmann::json ok;
    ok["recipeVersion"] = "1.0.0";
    ok["steps"] = nlohmann::json::array();
    CHECK(mm::validateRecipeJsonHeader(ok, err), "c: gecerli recipe REDDEDILDI");
    nlohmann::json bad = ok;
    bad["recipeVersion"] = "2.0.0";
    err.clear();
    CHECK(!mm::validateRecipeJsonHeader(bad, err), "c: major 2 KABUL EDILMEMELIYDI");
    CHECK(!err.empty(), "c: major 2 ret mesaji bos");
    err.clear();
    CHECK(!mm::validateRecipeJsonHeader(nlohmann::json::object(), err),
          "c: recipeVersion'suz recipe KABUL EDILMEMELIYDI");
}

// --- g) ayni roleKey + dislayici `when` --------------------------------------
static Recipe twoWeldRecipe(const std::string& whenA, const std::string& whenB) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    for (int i = 0; i < 2; ++i) {
        RecipeStep s;
        s.id = std::string("w") + (i == 0 ? "0" : "1");
        s.use = "weld.fillet";
        s.role = "weld_main";
        s.when = (i == 0 ? whenA : whenB);
        s.args["roleA"].text = "main";
        s.args["roleB"].text = "secondary_0";
        s.args["size"].text = "5";
        r.steps.push_back(s);
    }
    return r;
}

static void sectionG() {
    std::printf("[g] ayni roleKey + dislayici when\n");
    std::vector<CompParamSpec> specs(1);
    specs[0].name = L"x";
    specs[0].type = CompParamType::Length;
    specs[0].defDouble = 1.0;

    // 1) ayni roleKey + AYNI when -> statik RED
    const RecipeValidation v1 = validateRecipe(twoWeldRecipe("param.x > 0", "param.x > 0"), specs);
    CHECK(!v1.ok, "g: ayni roleKey + ayni when KABUL EDILMEMELIYDI");

    // 2) ayni roleKey + FARKLI when -> KABUL
    const Recipe rec = twoWeldRecipe("param.x > 0", "param.x < 0");
    const RecipeValidation v2 = validateRecipe(rec, specs);
    CHECK(v2.ok, "g: dislayici when'li yinelenen roleKey KABUL EDILMELIYDI");

    ComponentInput in;
    in.params.push_back(CompParam{L"x", 5.0});

    // 3) yalniz bir kosul saglanir -> tek kaynak
    const GenerationPlan p1 = runRecipe(rec, specs, in);
    CHECK(p1.ok, "g: tek kosul saglaninca plan gecerli olmali");
    CHECK(p1.welds.size() == 1, "g: tek kosul saglaninca TEK kaynak uretilmeli");

    // 4) ikisi birden saglanir -> calisma aninda RED
    const Recipe both = twoWeldRecipe("param.x > 0", "param.x > 1");
    const RecipeValidation v3 = validateRecipe(both, specs);
    CHECK(v3.ok, "g: farkli when metinleri statik olarak kabul edilir");
    const GenerationPlan p2 = runRecipe(both, specs, in);
    CHECK(!p2.ok, "g: iki kosul da saglanirsa calisma aninda RED");
    CHECK(p2.error.find(L"iki kez uretildi") != std::wstring::npos,
          "g: ret mesaji Turkce ve aciklayici olmali");
}

// --- d) frame.member_end {end: lower|upper} ----------------------------------
static GenerationPlan endFramePlan(const std::string& end) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep f;
    f.id = "f1";
    f.use = "frame.member_end";
    f.args["member"].text = "main";
    f.args["end"].text = end;
    r.steps.push_back(f);
    RecipeStep p;
    p.id = "p1";
    p.use = "plate.rect";
    p.role = "alt_levha";
    p.args["frame"].text = "f1";
    p.args["w"].text = "200";
    p.args["h"].text = "200";
    p.args["t"].text = "10";
    r.steps.push_back(p);
    ComponentInput in;
    // KOLON YUKARIDAN ASAGI cizilmis: axisFrom Z=3000, axisTo Z=0.
    in.main = memberGeometryFromProfile({0, 0, 3000}, {0, 0, 0}, /*connectionAtA=*/false,
                                        {1, 0, 0}, 0.0,
                                        MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
    return runRecipe(r, {}, in);
}

static void sectionD() {
    std::printf("[d] frame.member_end lower|upper (dunya Z)\n");
    const GenerationPlan lower = endFramePlan("lower");
    CHECK(lower.ok && lower.plates.size() == 1, "d: lower plan uretilmeli");
    if (lower.ok && !lower.plates.empty())
        CHECK(std::fabs(lower.plates[0].center.z) < 1e-6, "d: lower ucu Z=0 olmali");
    const GenerationPlan upper = endFramePlan("upper");
    CHECK(upper.ok && upper.plates.size() == 1, "d: upper plan uretilmeli");
    if (upper.ok && !upper.plates.empty())
        CHECK(std::fabs(upper.plates[0].center.z - 3000.0) < 1e-6, "d: upper ucu Z=3000 olmali");
    // 'start'/'end' eski davranisi korunur (axisFrom = 3000, axisTo = 0)
    const GenerationPlan start = endFramePlan("start");
    if (start.ok && !start.plates.empty())
        CHECK(std::fabs(start.plates[0].center.z - 3000.0) < 1e-6, "d: start = axisFrom (3000)");
    // gecersiz deger -> ACIK ret
    const GenerationPlan bad = endFramePlan("asagi");
    CHECK(!bad.ok, "d: gecersiz end degeri REDDEDILMELIYDI");
}

// --- i) frame.member_face {face: ...} ----------------------------------------
static GenerationPlan facePlan(const std::string& face, double at) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep f;
    f.id = "f1";
    f.use = "frame.member_face";
    f.args["member"].text = "main";
    f.args["face"].text = face;
    f.args["at"].text = std::to_string(static_cast<int>(at));
    r.steps.push_back(f);
    RecipeStep p;
    p.id = "p1";
    p.use = "plate.rect";
    p.role = "yuz_levhasi";
    p.args["frame"].text = "f1";
    p.args["w"].text = "100";
    p.args["h"].text = "100";
    p.args["t"].text = "10";
    r.steps.push_back(p);
    ComponentInput in;
    in.main = memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
                                        {1, 0, 0}, 0.0,
                                        MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
    return runRecipe(r, {}, in);
}

static void sectionI() {
    std::printf("[i] frame.member_face face: web/flange (toward'suz)\n");
    const GenerationPlan webP = facePlan("web_pos", 0.0);
    CHECK(webP.ok && webP.plates.size() == 1, "i: web_pos plan uretilmeli");
    const GenerationPlan webN = facePlan("web_neg", 0.0);
    CHECK(webN.ok && webN.plates.size() == 1, "i: web_neg plan uretilmeli");
    const GenerationPlan flP = facePlan("flange_pos", 0.0);
    CHECK(flP.ok && flP.plates.size() == 1, "i: flange_pos plan uretilmeli");
    const GenerationPlan flN = facePlan("flange_neg", 0.0);
    CHECK(flN.ok && flN.plates.size() == 1, "i: flange_neg plan uretilmeli");
    if (webP.ok && webN.ok && !webP.plates.empty() && !webN.plates.empty()) {
        const Vec3 a = webP.plates[0].center;
        const Vec3 b = webN.plates[0].center;
        const double la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        CHECK(std::fabs(la - 5.5) < 1e-6, "i: web yuzu govde kalinligi/2 = 5.5 mm");
        CHECK(std::fabs(a.x + b.x) < 1e-6 && std::fabs(a.y + b.y) < 1e-6,
              "i: web_pos ve web_neg SIMETRIK olmali");
    }
    if (flP.ok && flN.ok && !flP.plates.empty() && !flN.plates.empty()) {
        const Vec3 a = flP.plates[0].center;
        const Vec3 b = flN.plates[0].center;
        const double la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        CHECK(std::fabs(la - 150.0) < 1e-6, "i: flans yuzu kesit yuksekligi/2 = 150 mm");
        CHECK(std::fabs(a.x + b.x) < 1e-6 && std::fabs(a.y + b.y) < 1e-6,
              "i: flange_pos ve flange_neg SIMETRIK olmali");
    }
    // `at`: eksen boyunca kaydirir
    const GenerationPlan at = facePlan("flange_pos", 1000.0);
    if (at.ok && !at.plates.empty())
        CHECK(std::fabs(at.plates[0].center.z - 1000.0) < 1e-6, "i: at=1000 -> Z=1000");
    // face + toward birlikte -> RED
    {
        Recipe r;
        r.recipeVersion = "1.0.0";
        RecipeStep f;
        f.id = "f1";
        f.use = "frame.member_face";
        f.args["member"].text = "main";
        f.args["face"].text = "web_pos";
        f.args["toward"].text = "secondary_0";
        r.steps.push_back(f);
        ComponentInput in;
        in.main = memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, false, {1, 0, 0}, 0.0,
                                            MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
        const GenerationPlan bad = runRecipe(r, {}, in);
        CHECK(!bad.ok, "i: face + toward birlikte REDDEDILMELIYDI");
    }
    // gecersiz face -> RED
    const GenerationPlan badFace = facePlan("ust_yuz", 0.0);
    CHECK(!badFace.ok, "i: gecersiz face REDDEDILMELIYDI");
}

// --- h) frame.bisector {a, b} -----------------------------------------------
static void sectionH() {
    std::printf("[h] frame.bisector (acirtay duzlemi)\n");
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep f;
    f.id = "bis";
    f.use = "frame.bisector";
    f.args["a"].text = "main";
    f.args["b"].text = "secondary_0";
    r.steps.push_back(f);
    RecipeStep p;
    p.id = "p1";
    p.use = "plate.rect";
    p.role = "acirtay_levhasi";
    p.args["frame"].text = "bis";
    p.args["w"].text = "100";
    p.args["h"].text = "100";
    p.args["t"].text = "10";
    r.steps.push_back(p);

    ComponentInput in;
    // KOSE: A +X yonunde, B +Z yonunda; bulusma noktasi orijin.
    in.main = memberGeometryFromProfile({0, 0, 0}, {3000, 0, 0}, /*connectionAtA=*/false,
                                        {1, 0, 0}, 0.0,
                                        MemberSectionSpec{200.0, 200.0, 8.0, 12.0, 0.0});
    in.secondary.push_back(memberGeometryFromProfile(
        {0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/true, {0, 0, 1}, 0.0,
        MemberSectionSpec{200.0, 200.0, 8.0, 12.0, 0.0}));

    const GenerationPlan plan = runRecipe(r, {}, in);
    if (!plan.ok) std::printf("      [teshis] hata: %ls\n", plan.error.c_str());
    CHECK(plan.ok && plan.plates.size() == 1, "h: acirtay levhasi uretilmeli");
    if (plan.ok && !plan.plates.empty()) {
        const PlateSpec& pl = plan.plates[0];
        const double s2 = std::sqrt(0.5);
        // u = normalize(dA+dB) = (0.707, 0, 0.707)
        CHECK(std::fabs(pl.u.x - s2) < 1e-6 && std::fabs(pl.u.z - s2) < 1e-6,
              "h: u = (0.707, 0, 0.707) olmali");
        // w = normalize(dA-dB) = (0.707, 0, -0.707)
        CHECK(std::fabs(pl.axis.x - s2) < 1e-6 && std::fabs(pl.axis.z + s2) < 1e-6,
              "h: w = (0.707, 0, -0.707) olmali");
        // u _|_ w
        const double dot = pl.u.x * pl.axis.x + pl.u.y * pl.axis.y + pl.u.z * pl.axis.z;
        CHECK(std::fabs(dot) < 1e-9, "h: u ve w DIK olmali");
        // bulusma noktasi orijinde
        CHECK(std::fabs(pl.center.x) < 1e-6 && std::fabs(pl.center.z) < 1e-6,
              "h: levha merkezi bulusma noktasinda olmali");
    }
    // ayni uye -> RED
    {
        Recipe bad;
        bad.recipeVersion = "1.0.0";
        RecipeStep s1;
        s1.id = "bis";
        s1.use = "frame.bisector";
        s1.args["a"].text = "main";
        s1.args["b"].text = "main";
        bad.steps.push_back(s1);
        const GenerationPlan bp = runRecipe(bad, {}, in);
        CHECK(!bp.ok, "h: ayni uye iki kez REDDEDILMELIYDI");
    }
    // paralel uyeler -> RED
    {
        ComponentInput par = in;
        par.secondary[0] = memberGeometryFromProfile({0, 0, 500}, {3000, 0, 500}, true,
                                                     {1, 0, 0}, 0.0,
                                                     MemberSectionSpec{200.0, 200.0, 8.0, 12.0, 0.0});
        const GenerationPlan pp = runRecipe(r, {}, par);
        CHECK(!pp.ok, "h: paralel uyelerde acirtay REDDEDILMELIYDI");
    }
}

// --- e) ankraj bulonu (boy / gomme / tip) ------------------------------------
static GenerationPlan anchorPlan(const std::string& anchorType, double len, double embed) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep f;
    f.id = "f1";
    f.use = "frame.member_end";
    f.args["member"].text = "main";
    f.args["end"].text = "lower";
    r.steps.push_back(f);
    RecipeStep b;
    b.id = "b1";
    b.use = "bolts.grid";
    b.role = "ankraj";
    b.args["frame"].text = "f1";
    b.args["nx"].text = "2";
    b.args["ny"].text = "2";
    b.args["px"].text = "100";
    b.args["py"].text = "100";
    if (!anchorType.empty()) {
        b.args["anchorType"].text = anchorType;
        b.args["anchorLength"].text = std::to_string(static_cast<int>(len));
        b.args["anchorEmbed"].text = std::to_string(static_cast<int>(embed));
    }
    r.steps.push_back(b);
    ComponentInput in;
    in.main = memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
                                        {1, 0, 0}, 0.0,
                                        MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
    return runRecipe(r, {}, in);
}

static void sectionE() {
    std::printf("[e] ankraj bulonu: boy / gomme / tip\n");
    const GenerationPlan an = anchorPlan("cast-in", 300.0, 200.0);
    CHECK(an.ok && an.boltGroups.size() == 1, "e: ankraj plani uretilmeli");
    if (an.ok && !an.boltGroups.empty()) {
        const BoltGroup& g = an.boltGroups[0];
        CHECK(g.anchorType == "cast-in", "e: anchorType cast-in olmali");
        CHECK(std::fabs(g.anchorLength - 300.0) < 1e-9, "e: anchorLength 300");
        CHECK(std::fabs(g.anchorEmbed - 200.0) < 1e-9, "e: anchorEmbed 200");
    }
    // antrasiz (normal) grup: alanlar bos/0
    const GenerationPlan nb = anchorPlan("", 0.0, 0.0);
    CHECK(nb.ok && nb.boltGroups.size() == 1, "e: normal grup uretilmeli");
    if (nb.ok && !nb.boltGroups.empty())
        CHECK(nb.boltGroups[0].anchorType.empty(), "e: normal grupta anchorType BOS olmali");

    // kanonik JSON: normal grupta ankraj anahtarlari YOK (digest degismez)
    const std::string normalJson = generationPlanCanonicalJson(nb);
    CHECK(normalJson.find("anchorType") == std::string::npos,
          "e: normal grupta 'anchorType' anahtari YAZILMAMALI");
    const std::string anchorJson = generationPlanCanonicalJson(an);
    CHECK(anchorJson.find("anchorType") != std::string::npos,
          "e: ankraj grubunda 'anchorType' anahtari YAZILMALI");

    // gomme > boy -> RED
    const GenerationPlan bad = anchorPlan("cast-in", 200.0, 300.0);
    CHECK(!bad.ok, "e: anchorEmbed > anchorLength REDDEDILMELIYDI");
    // negatif -> RED
    const GenerationPlan neg = anchorPlan("cast-in", -10.0, 0.0);
    CHECK(!neg.ok, "e: negatif anchorLength REDDEDILMELIYDI");
}

// --- j) bolts.grid esit olmayan aralik (xs/ys, Tekla yazimi) ------------------
static GenerationPlan spacingPlan(const std::vector<std::string>& xs,
                                  const std::vector<std::string>& ys) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep f;
    f.id = "f1";
    f.use = "frame.member_end";
    f.args["member"].text = "main";
    f.args["end"].text = "lower";
    r.steps.push_back(f);
    RecipeStep b;
    b.id = "b1";
    b.use = "bolts.grid";
    b.role = "bulonlar";
    b.args["frame"].text = "f1";
    b.args["d"].text = "20";
    b.args["hole"].text = "22";
    if (!xs.empty()) {
        RecipeArg list;
        list.isList = true;
        for (const auto& s : xs) { RecipeArg it; it.text = s; list.items.push_back(it); }
        b.args["xs"] = list;
    }
    if (!ys.empty()) {
        RecipeArg list;
        list.isList = true;
        for (const auto& s : ys) { RecipeArg it; it.text = s; list.items.push_back(it); }
        b.args["ys"] = list;
    }
    r.steps.push_back(b);
    ComponentInput in;
    in.main = memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
                                        {1, 0, 0}, 0.0,
                                        MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
    return runRecipe(r, {}, in);
}

static void sectionJ() {
    std::printf("[j] bolts.grid xs/ys (Tekla aralik yazimi)\n");
    // "70 2*80 70" tek oge icinde: 4 aralik -> 5 bulon
    const GenerationPlan p1 = spacingPlan({"70 2*80 70"}, {});
    CHECK(p1.ok && p1.boltGroups.size() == 1, "j: xs'li plan uretilmeli");
    if (p1.ok && !p1.boltGroups.empty()) {
        const BoltGroup& g = p1.boltGroups[0];
        CHECK(g.countX == 5, "j: countX = aralik sayisi + 1 = 5");
        CHECK(g.spacingXs.size() == 4, "j: 4 aralik yazilmali");
        if (g.spacingXs.size() == 4) {
            CHECK(std::fabs(g.spacingXs[0] - 70.0) < 1e-9, "j: 1. aralik 70");
            CHECK(std::fabs(g.spacingXs[1] - 80.0) < 1e-9, "j: 2. aralik 80");
            CHECK(std::fabs(g.spacingXs[2] - 80.0) < 1e-9, "j: 3. aralik 80");
            CHECK(std::fabs(g.spacingXs[3] - 70.0) < 1e-9, "j: 4. aralik 70");
        }
    }
    // liste formu: iki oge, her biri tek sayi -> 2 aralik, 3 bulon
    const GenerationPlan p2 = spacingPlan({"60", "90"}, {"40", "40"});
    if (p2.ok && !p2.boltGroups.empty()) {
        const BoltGroup& g = p2.boltGroups[0];
        CHECK(g.countX == 3 && g.countY == 3, "j: 2+1 x 2+1 bulon");
        CHECK(g.spacingXs.size() == 2 && g.spacingYs.size() == 2, "j: 2 x 2 aralik");
    }
    // xs yok -> eski davranis (nx/px)
    const GenerationPlan p3 = spacingPlan({}, {});
    if (p3.ok && !p3.boltGroups.empty()) {
        CHECK(p3.boltGroups[0].countX == 1 && p3.boltGroups[0].spacingXs.empty(),
              "j: xs yoksa countX=1 ve aralik listesi bos");
    }
    // gecersiz yazim -> RED
    const GenerationPlan bad = spacingPlan({"70 abc 70"}, {});
    CHECK(!bad.ok, "j: gecersiz aralik yazimi REDDEDILMELIYDI");
    const GenerationPlan neg = spacingPlan({"-10"}, {});
    CHECK(!neg.ok, "j: negatif aralik REDDEDILMELIYDI");
}

// --- k) weld.fillet: tip / kismi kaynak / cevre / santiye --------------------
static GenerationPlan weldPlan(const std::string& type, double len, double off,
                               bool around, bool site) {
    Recipe r;
    r.recipeVersion = "1.0.0";
    RecipeStep w;
    w.id = "w1";
    w.use = "weld.fillet";
    w.role = "kose_kaynagi";
    w.args["roleA"].text = "main";
    w.args["roleB"].text = "secondary_0";
    w.args["size"].text = "6";
    if (!type.empty()) w.args["type"].text = type;
    if (len != 0.0) w.args["length"].text = std::to_string(static_cast<int>(len));
    if (off != 0.0) w.args["offset"].text = std::to_string(static_cast<int>(off));
    if (around) w.args["around"].text = "true";
    if (site) w.args["site"].text = "true";
    r.steps.push_back(w);
    ComponentInput in;
    in.main = memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, false, {1, 0, 0}, 0.0,
                                        MemberSectionSpec{300.0, 300.0, 11.0, 19.0, 0.0});
    in.secondary.push_back(memberGeometryFromProfile({0, 0, 2880}, {4000, 0, 2880}, true,
                                                     {1, 0, 0}, 0.0,
                                                     MemberSectionSpec{200.0, 200.0, 8.0, 12.0, 0.0}));
    return runRecipe(r, {}, in);
}

static void sectionK() {
    std::printf("[k] weld.fillet tip/kismi/cevre/santiye\n");
    const GenerationPlan full = weldPlan("", 0.0, 0.0, false, false);
    CHECK(full.ok && full.welds.size() == 1, "k: varsayilan kaynak uretilmeli");
    if (full.ok && !full.welds.empty()) {
        CHECK(full.welds[0].type.empty(), "k: varsayilan type BOS (fillet)");
        const std::string cj = generationPlanCanonicalJson(full);
        CHECK(cj.find("\"site\"") == std::string::npos, "k: varsayilan kaynakta 'site' YOK");
        CHECK(cj.find("\"around\"") == std::string::npos, "k: varsayilan kaynakta 'around' YOK");
        CHECK(cj.find("\"type\"") == std::string::npos, "k: varsayilan kaynakta 'type' YOK");
    }
    const GenerationPlan butt = weldPlan("butt", 120.0, 30.0, true, true);
    if (!butt.ok) std::printf("      [teshis] k hata: %ls\n", butt.error.c_str());
    CHECK(butt.ok && butt.welds.size() == 1, "k: kismi küt kaynak uretilmeli");
    if (butt.ok && !butt.welds.empty()) {
        const WeldSpec& wl = butt.welds[0];
        CHECK(wl.type == "butt", "k: type butt");
        CHECK(std::fabs(wl.length - 120.0) < 1e-9, "k: length 120");
        CHECK(std::fabs(wl.offset - 30.0) < 1e-9, "k: offset 30");
        CHECK(wl.around, "k: around true");
        CHECK(wl.site, "k: site (santiye) true");
        const std::string cj = generationPlanCanonicalJson(butt);
        CHECK(cj.find("\"butt\"") != std::string::npos, "k: kanonik JSON'da type butt yazilmali");
    }
    // gecersiz tip -> RED
    const GenerationPlan bad = weldPlan("yari", 0.0, 0.0, false, false);
    CHECK(!bad.ok, "k: gecersiz type REDDEDILMELIYDI");
    // negatif uzunluk -> RED
    const GenerationPlan neg = weldPlan("fillet", 0.0, -5.0, false, false);
    CHECK(!neg.ok, "k: negatif offset REDDEDILMELIYDI");
}

int main() {
    std::printf("contract %s\n", kContractVersion);
    sectionA();
    sectionB();
    sectionC();
    sectionD();
    sectionI();
    sectionH();
    sectionE();
    sectionJ();
    sectionK();
    sectionG();
    if (failures == 0) std::printf("TUM TESTLER GECTI\n");
    else std::printf("%d HATA\n", failures);
    return failures == 0 ? 0 : 1;
}
