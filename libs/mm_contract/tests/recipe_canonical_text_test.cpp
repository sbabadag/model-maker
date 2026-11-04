// ============================================================================
// KANONIK RECIPE/PAKET METIN YAZICISI (F) testi.
//
// recipeCanonicalText: anahtar sirasi (kok = recipeVersion, let, steps; adim =
// id, use, role, when, check, message, args), args alfabetik, formatVersion YOK,
// dump(2) + tek '\n', LF-only. packageJsonText: dump(2)+'\n'. writePackageJsonFile:
// CR bayti iceren metni REDDEDER, LF-only yazar.
// ============================================================================
#include "model_maker/component_codec.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static RecipeArg S(const std::string& t) { RecipeArg a; a.text = t; return a; }

static std::size_t pos(const std::string& hay, const std::string& needle) {
    return hay.find("\"" + needle + "\"");
}

int main() {
    // --- ornek recipe: arg sirasi bilerek karisik ---
    Recipe r;
    r.recipeVersion = "1.0.0";
    r.lets.emplace_back("alpha", "1");
    r.lets.emplace_back("beta", "2");
    RecipeStep s;
    s.id = "ep";
    s.use = "endplate.placement";
    s.role = "end_plate";
    s.when = "param.x > 0";
    s.args["main"] = S("main");
    s.args["beam"] = S("secondary_0");
    s.args["rolePlate"] = S("end_plate");
    s.args["roleBolts"] = S("beam_bolts");
    r.steps.push_back(s);

    const std::string text = recipeCanonicalText(r);

    // 1) formatVersion YOK.
    CHECK(pos(text, "formatVersion") == std::string::npos, "formatVersion yok");

    // 2) kok anahtar sirasi: recipeVersion < let < steps.
    const std::size_t pRv = pos(text, "recipeVersion");
    const std::size_t pLet = pos(text, "let");
    const std::size_t pSteps = pos(text, "steps");
    CHECK(pRv != std::string::npos && pLet != std::string::npos && pSteps != std::string::npos,
          "kok anahtarlar mevcut");
    CHECK(pRv < pLet && pLet < pSteps, "kok anahtar sirasi recipeVersion < let < steps");

    // 3) adim anahtar sirasi: id < use < role < when < args (check/message bos).
    const std::size_t pId = pos(text, "id");
    const std::size_t pUse = pos(text, "use");
    const std::size_t pRole = pos(text, "role");
    const std::size_t pWhen = pos(text, "when");
    const std::size_t pArgs = pos(text, "args");
    CHECK(pId < pUse && pUse < pRole && pRole < pWhen && pWhen < pArgs,
          "adim anahtar sirasi id < use < role < when < args");

    // 4) args ALFABETIK: beam < main < roleBolts < rolePlate.
    const std::size_t pBeam = pos(text, "beam");
    const std::size_t pMain = pos(text, "main");
    const std::size_t pRoleBolts = pos(text, "roleBolts");
    const std::size_t pRolePlate = pos(text, "rolePlate");
    CHECK(pBeam < pMain && pMain < pRoleBolts && pRoleBolts < pRolePlate,
          "args alfabetik (beam < main < roleBolts < rolePlate)");

    // 5) tek '\n' ile bitiyor, CR YOK.
    CHECK(!text.empty() && text.back() == '\n', "sonda tek LF");
    CHECK(text.find('\r') == std::string::npos, "CR yok");
    CHECK(text.size() < 2 || text[text.size() - 2] != '\n', "yalniz tek '\n' (cift LF yok)");

    // 6) round-trip: recipeFromJson(parse(text)) == orijinal (idempotent icerik).
    const Recipe back = recipeFromJson(nlohmann::json::parse(text, nullptr, false));
    CHECK(back.recipeVersion == r.recipeVersion, "roundtrip recipeVersion");
    CHECK(back.steps.size() == 1, "roundtrip adim sayisi");
    if (back.steps.size() == 1) {
        const RecipeStep& b = back.steps[0];
        CHECK(b.id == "ep" && b.use == "endplate.placement" && b.role == "end_plate" &&
              b.when == "param.x > 0", "roundtrip adim alanlari");
        CHECK(b.args.size() == 4, "roundtrip arg sayisi");
        CHECK(b.args.at("beam").text == "secondary_0" && b.args.at("main").text == "main" &&
              b.args.at("rolePlate").text == "end_plate" && b.args.at("roleBolts").text == "beam_bolts",
              "roundtrip arg degerleri");
    }
    // lets round-trip (sirali).
    CHECK(back.lets.size() == 2 && back.lets[0].first == "alpha" && back.lets[0].second == "1" &&
          back.lets[1].first == "beta" && back.lets[1].second == "2",
          "roundtrip let (sirali)");

    // 7) idempotent: zaten kanonik metni tekrar kanoniklestirmek ayni bayti verir.
    const std::string text2 = recipeCanonicalText(back);
    CHECK(text2 == text, "recipeCanonicalText idempotent (bayt-ayni)");

    // 8) packageJsonText: dump(2)+'\n', CR yok.
    const nlohmann::json j = {{"b", 1}, {"a", 2}};
    const std::string pj = packageJsonText(j);
    CHECK(pj.back() == '\n', "packageJsonText sonda LF");
    CHECK(pj.find('\r') == std::string::npos, "packageJsonText CR yok");
    CHECK(pos(pj, "a") < pos(pj, "b"), "packageJsonText anahtar sirali (nlohmann std::map)");

    // 9) writePackageJsonFile: CR'li metni REDDEDER; LF-only yazar.
    {
        const std::string tmp = "/tmp/recipe_canonical_text_probe.json";
        std::remove(tmp.c_str());  // onceki kosudan kalan dosyayi temizle
        const bool rejected = !writePackageJsonFile(tmp, "{\"a\":1}\r\n");
        CHECK(rejected, "writePackageJsonFile CR'li metni reddetti");
        std::ifstream in(tmp, std::ios::binary);
        CHECK(!in.good(), "CR'li metin dosyaya yazilmadi");

        const bool okWrite = writePackageJsonFile(tmp, "{\"a\":1}\n");
        CHECK(okWrite, "writePackageJsonFile LF-only yazdi");
        std::ifstream in2(tmp, std::ios::binary);
        std::string buf((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
        CHECK(buf == "{\"a\":1}\n", "yazilan metin bayt-ayni (LF-only)");
    }

    std::printf("recipe kanonik metin testi: %s (%d hata)\n",
                failures ? "BASARISIZ" : "OK", failures);
    return failures ? 1 : 0;
}
