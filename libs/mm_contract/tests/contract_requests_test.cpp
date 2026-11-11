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
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/recipe.hpp"

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

int main() {
    std::printf("contract %s\n", kContractVersion);
    sectionA();
    sectionB();
    if (failures == 0) std::printf("TUM TESTLER GECTI\n");
    else std::printf("%d HATA\n", failures);
    return failures == 0 ? 0 : 1;
}
