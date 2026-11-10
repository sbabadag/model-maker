// MM-5 — COMPONENT PAKET YUKLEYICI (.mmcpkg) uygulamasi.
//
// Dogrulama sirasi COMPONENT-PACKAGE-01 §4.4 ile BIREBIR ayni:
//   checksums -> manifest -> contract surumu -> parameters -> dialog -> recipe
//   -> altin testler.
// Herhangi bir adimda hata -> tip KAYDEDILMEZ (fail-closed), mesaj Turkce.
//
// OCCT/Qt/windows.h YOK: saf std + nlohmann/json + mevcut contract katmani.

#include "model_maker/component_package.hpp"

#include "model_maker/component_codec.hpp"
#include "model_maker/sha256.hpp"
#include "mm_contract/contract_version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using nlohmann::json;

namespace mm {
namespace {

// --- kucuk yardimcilar -------------------------------------------------------

std::wstring wideAscii(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

std::wstring pathText(const fs::path& p) {
    // Hata mesajlari icin: dar (ASCII) temsil yeterli; wide yol donusumu
    // platforma ozel API gerektirir ve bu katman windows.h KULLANMAZ.
    return wideAscii(p.string());
}

bool readFileBytes(const fs::path& p, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool readJsonFile(const fs::path& p, json& out, std::wstring& err) {
    std::string text;
    if (!readFileBytes(p, text)) {
        err = L"dosya okunamadi: " + pathText(p);
        return false;
    }
    // MM-5 (Windows bulgusu): paketler BYTE-ayni olmali; CRLF donusumu
    // checksums.json'u bozar ve paket sessizce reddedilir. Sebebi ACIKCA yaz.
    // G: YALNIZ CRLF degil, TEK BASINA CR bayti da REDDEDILIR (Studio ayni
    // kurali uygular; core.autocrlf veya elle CR sokulmasi paketi bozar).
    if (text.find('\r') != std::string::npos) {
        err = L"dosya CR bayti iceriyor (CRLF veya yalniz CR; Windows core.autocrlf): " +
              pathText(p.filename()) +
              L" — .gitattributes 'libs/mm_contract/packages/** -text' gerekli";
        return false;
    }
    try {
        out = json::parse(text);
    } catch (const std::exception& ex) {
        err = L"JSON ayristirilamadi (" + pathText(p.filename()) + L"): " +
              wideAscii(ex.what());
        return false;
    }
    return true;
}

bool parseSemver(const std::string& s, int& maj, int& min, int& patch);  // asagida

// ST-3g FAIL-CLOSED: recipe.json basligi ve arguman TURLERI.
//  - recipeVersion zorunlu, semver, major 1 olmali (2.0.0 REDDEDILIR);
//  - arguman degeri JSON boolean olamaz: metin ifade ("2*e") ya da sayi
//    beklenir. Onceden boolean "1" metnine cevrilip sessizce gecerdi.
bool validateRecipeJsonHeader(const json& rj, std::wstring& err) {
    if (!rj.is_object()) { err = L"recipe.json nesne degil"; return false; }
    if (!rj.contains("recipeVersion") || !rj["recipeVersion"].is_string()) {
        err = L"recipe.json recipeVersion eksik (string semver beklenir)";
        return false;
    }
    const std::string ver = rj["recipeVersion"].get<std::string>();
    int maj = 0, mnr = 0, pat = 0;
    if (!parseSemver(ver, maj, mnr, pat)) {
        err = L"recipe.json recipeVersion semver degil: " + wideAscii(ver);
        return false;
    }
    if (maj != 1) {
        err = L"recipe.json recipeVersion desteklenmiyor: " + wideAscii(ver) +
              L" (bu surum yalniz 1.x recipe bicimini okur)";
        return false;
    }
    // G: let bolumu — RECIPE.md: "let: ad -> ifade". let bir NESNE olmali ve her
    // degeri IFADE METNI (string) olmali. Boolean / liste / sayi / nesne olmayan
    // degerler sessizce atlanmaz, alan adiyla ACIK reddedilir (fail-closed).
    if (rj.contains("let")) {
        if (!rj["let"].is_object()) {
            err = L"recipe.json 'let' nesne degil (let: ad -> ifade beklenir)";
            return false;
        }
        for (auto it = rj["let"].begin(); it != rj["let"].end(); ++it) {
            if (!it.value().is_string()) {
                const char* kind = it.value().is_boolean()  ? "boolean"
                                   : it.value().is_array()  ? "liste"
                                   : it.value().is_number() ? "sayi"
                                                            : "nesne/gecersiz";
                err = L"recipe.json let '" + wideAscii(it.key()) +
                      L"' ifade metni degil (" + wideAscii(kind) +
                      L"); ad -> ifade (string) beklenir";
                return false;
            }
        }
    }
    if (!rj.contains("steps")) { err = L"recipe.json steps eksik"; return false; }
    if (!rj["steps"].is_array()) { err = L"recipe.json steps dizi degil"; return false; }
    std::size_t index = 0;
    for (const auto& step : rj["steps"]) {
        if (!step.is_object()) {
            err = L"recipe.json adim " + std::to_wstring(index) + L" nesne degil";
            return false;
        }
        if (step.contains("args")) {
            if (!step["args"].is_object()) {
                err = L"recipe.json adim " + std::to_wstring(index) + L": args nesne degil";
                return false;
            }
            const std::string use = step.contains("use") && step["use"].is_string()
                                        ? step["use"].get<std::string>() : std::string("?");
            for (auto it = step["args"].begin(); it != step["args"].end(); ++it) {
                // Dizi icindeki boolean'lar da ayni kurala tabidir.
                std::function<bool(const json&, std::string&)> bad =
                    [&](const json& v, std::string& why) -> bool {
                    if (v.is_boolean()) { why = "boolean"; return true; }
                    if (v.is_array())
                        for (const auto& e : v) if (bad(e, why)) return true;
                    if (v.is_object())
                        for (auto i2 = v.begin(); i2 != v.end(); ++i2) if (bad(i2.value(), why)) return true;
                    return false;
                };
                std::string why;
                if (bad(it.value(), why)) {
                    err = L"recipe.json adim " + std::to_wstring(index) + L" (" +
                          wideAscii(use) + L") argumani '" + wideAscii(it.key()) +
                          L"' BOOLEAN; metin ifade veya sayi beklenir "
                          L"(true/false sessizce \"1\"/\"0\" yapilmaz)";
                    return false;
                }
            }
        }
        ++index;
    }
    return true;
}

// checksums.json anahtari guvenli goreli yol mu? ("../", mutlak yol ve surucu
// oneki REDDEDILIR — paket disina cikilamaz.)
bool isSafeRelativeKey(const std::string& key) {
    if (key.empty() || key.size() > 512) return false;
    if (key.front() == '/' || key.front() == '\\') return false;
    if (key.find(':') != std::string::npos) return false;
    if (key.find("..") != std::string::npos) return false;
    if (key.find('\0') != std::string::npos) return false;
    return true;
}

bool parseSemver(const std::string& s, int& maj, int& min, int& patch) {
    int a = 0, b = 0, c = 0;
    if (std::sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c) != 3) return false;
    if (a < 0 || b < 0 || c < 0) return false;
    maj = a;
    min = b;
    patch = c;
    return true;
}

// typeId sozlesmesi: [a-z0-9_]+ (kalici kimlik; bos/buyuk harf/tire RED).
bool isValidTypeId(const std::wstring& id) {
    if (id.empty() || id.size() > 64) return false;
    for (wchar_t c : id) {
        const bool ok = (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_';
        if (!ok) return false;
    }
    return true;
}

// manifest.displayName: string VEYA {"tr": "...", "en": "..."} kabul edilir.
std::wstring displayNameFromJson(const json& j) {
    if (j.is_string()) return wideFromUtf8(j.get<std::string>());
    if (j.is_object()) {
        for (const char* key : {"tr", "en"}) {
            if (j.contains(key) && j[key].is_string())
                return wideFromUtf8(j[key].get<std::string>());
        }
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.value().is_string()) return wideFromUtf8(it.value().get<std::string>());
    }
    return {};
}

// P-1/4 — RECIPE'DEN SECIM SAYISI CIKARIMI.
//
// Studio manifesti 'catalog' blogu yazmadiginda komponent katalogda
// gorunmuyordu (catalogVisible varsayilani false) ve pickCount 2 varsayiliyordu
// (taban levhasi gibi TEK uyeli paketler "tam 2 parca secin" ile reddediliyordu).
// Recipe uye referanslari GERCEK kaynaktir: "main" -> 1, "secondary_<n>" -> n+2.
// Metin ifadelerin (orn "frame.member_end(main, secondary_0)") icinde de aranir.
int inferPickCountFromRecipeJson(const json& j) {
    int maxSecondary = -1;
    bool anyMain = false;
    const std::function<void(const json&)> walk = [&](const json& node) {
        if (node.is_string()) {
            const std::string t = node.get<std::string>();
            for (std::size_t pos = t.find("secondary_"); pos != std::string::npos;
                 pos = t.find("secondary_", pos + 1)) {
                std::size_t i = pos + 10;
                int n = 0;
                bool digits = false;
                while (i < t.size() && t[i] >= '0' && t[i] <= '9' && n < 1000) {
                    n = n * 10 + (t[i] - '0');
                    digits = true;
                    ++i;
                }
                if (digits && n > maxSecondary) maxSecondary = n;
            }
            // "secondary" (indekssiz) = 0. uye
            if (t.find("secondary") != std::string::npos && maxSecondary < 0) maxSecondary = 0;
            if (t.find("main") != std::string::npos) anyMain = true;
            return;
        }
        if (node.is_array()) {
            for (const json& v : node) walk(v);
            return;
        }
        if (node.is_object()) {
            for (auto it = node.begin(); it != node.end(); ++it) walk(it.value());
        }
    };
    walk(j);
    if (maxSecondary >= 0) return maxSecondary + 2;
    return anyMain ? 1 : 2;  // bilinmiyorsa guvenli varsayilan: 2 uye
}

ComponentPackageLoad fail(ComponentPackageLoad r, std::wstring msg) {
    r.ok = false;
    r.message = std::move(msg);
    return r;
}

}  // namespace

// --- generik generator -------------------------------------------------------

ComponentGenerator makeRecipeGenerator(ComponentDialogDefinition dialog, Recipe recipe) {
    std::vector<CompParamSpec> specs = std::move(dialog.specs);
    return ComponentGenerator([specs = std::move(specs), recipe = std::move(recipe)](
                                  const ComponentInput& in) -> GenerationPlan {
        return runRecipe(recipe, specs, in);
    });
}

// --- tek paket ---------------------------------------------------------------

namespace {
// `allowExistingPackage` (P-1/3): tip daha once PAKETTEN yuklendiyse cakisma
// REDDEDILMEZ (surum karsilastirmasi cagiranda). Yerlesik tip HER ZAMAN kazanir.
ComponentPackageLoad loadPackageImpl(const fs::path& dir, const ComponentRegistry& registry,
                                     bool allowExistingPackage) {
    ComponentPackageLoad out;
    out.dir = dir;
    // P-1/4: katalog meta alanlari manifestte ACIKCA verildi mi? Verilmediyse
    // paket icin guvenli varsayilanlar uygulanir (gorunur + recipe'den
    // cikarilan secim sayisi).
    bool catalogVisibleExplicit = false;
    bool pickCountExplicit = false;

    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return fail(out, L"paket klasoru yok: " + pathText(dir));

    // (1) CHECKSUMS — varsa ZORUNLU dosyalari da kapsamali ve tutmali.
    {
        const fs::path cf = dir / kPackageChecksumsFile;
        if (fs::is_regular_file(cf, ec)) {
            json cj;
            std::wstring err;
            if (!readJsonFile(cf, cj, err)) return fail(out, err);
            if (!cj.is_object()) return fail(out, L"checksums.json nesne degil");
            const std::set<std::string> required = {
                kPackageManifestFile, kPackageParametersFile,
                kPackageDialogFile, kPackageRecipeFile};
            std::set<std::string> covered;
            // P-1/6: PAKET ICERIK OZETI. Dosya ozetlerinin KANONIK bilesimi
            // (anahtar sirali) -> tek sha256. .mmw'ye yazilir; acilista paket
            // degismisse (Studio Ctrl+S) ornek yeniden uretilir.
            std::string contentAccum;
            for (auto it = cj.begin(); it != cj.end(); ++it) {
                const std::string key = it.key();
                if (!isSafeRelativeKey(key))
                    return fail(out, L"checksums.json guvensiz anahtar: " + wideAscii(key));
                if (!it.value().is_string())
                    return fail(out, L"checksums.json[" + wideAscii(key) + L"] string degil");
                std::string bytes;
                if (!readFileBytes(dir / key, bytes))
                    return fail(out, L"checksums.json[" + wideAscii(key) + L"] dosya yok");
                const std::string actual = sha256Hex(bytes);
                std::string expected = it.value().get<std::string>();
                std::transform(expected.begin(), expected.end(), expected.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (actual != expected) {
                    std::wstring msg = L"checksum UYUSMUYOR: " + wideAscii(key) +
                                       L" (beklenen " + wideAscii(expected.substr(0, 12)) +
                                       L"..., gercek " + wideAscii(actual.substr(0, 12)) + L")";
                    // HR-11: en sik neden CRLF donusumu; ham "checksum uyusmuyor" mesaji
                    // teshisi zorlastiriyordu. Dosyada CR bayti varsa NEDENI de yaz.
                    if (bytes.find('\r') != std::string::npos)
                        msg += L" — dosyada CR bayti var: satir sonlari CRLF'e donusmus "
                               L"(git core.autocrlf / .gitattributes 'libs/mm_contract/packages/** -text')";
                    return fail(out, msg);
                }
                covered.insert(key);
                contentAccum += key + ":" + actual + "\n";
            }
            for (const auto& r : required) {
                if (!covered.count(r))
                    return fail(out, L"checksums.json zorunlu dosyayi kapsamiyor: " + wideAscii(r));
            }
            out.type.packageChecksum = sha256Hex(contentAccum);
        } else {
            out.warnings.push_back(L"checksums.json yok — butunluk dogrulamasi atlandi");
        }
    }

    // (2) MANIFEST
    json mj;
    {
        std::wstring err;
        if (!readJsonFile(dir / kPackageManifestFile, mj, err)) return fail(out, err);
        if (!mj.is_object()) return fail(out, L"manifest.json nesne degil");
        if (mj.contains("format") && mj["format"].is_string() &&
            mj["format"].get<std::string>() != "mmcpkg")
            return fail(out, L"manifest.format 'mmcpkg' degil");
        if (!mj.contains("typeId") || !mj["typeId"].is_string())
            return fail(out, L"manifest.typeId eksik");
        out.typeId = wideFromUtf8(mj["typeId"].get<std::string>());
        if (!isValidTypeId(out.typeId))
            return fail(out, L"manifest.typeId gecersiz (yalniz [a-z0-9_], <=64): " + out.typeId);
        if (mj.contains("formatVersion") && (!mj["formatVersion"].is_string() ||
                                              mj["formatVersion"].get<std::string>() != "1.0.0"))
            return fail(out, L"desteklenmeyen manifest formatVersion (beklenen 1.0.0)");
        // FAIL-CLOSED (ST-3g): displayName ZORUNLU. Onceden yoksa typeId'ye
        // dusuyordu; Studio ve katalog arayuzu bos/teknik ad gosteriyordu.
        if (!mj.contains("displayName"))
            return fail(out, L"manifest.displayName eksik (gorunen ad zorunlu)");
        out.displayName = displayNameFromJson(mj["displayName"]);
        if (out.displayName.empty())
            return fail(out, L"manifest.displayName bos ya da gecersiz "
                             L"(string veya {\"tr\":\"...\"} beklenir)");
        out.type.typeId = out.typeId;
        out.type.displayName = out.displayName;
        out.type.schemaVersion = mj.value("schemaVersion", 1);
        // P-1/4: paket tipleri KATALOGDA GORUNUR (acikca false denmedikce).
        // Yerlesik tiplerde opt-in (catalogVisible) mantigi korunur; paket
        // yukleyicisi icin dogru varsayilan "gorunur"dur.
        if (mj.contains("catalog") && mj["catalog"].is_object()) {
            out.type.catalog = componentCatalogMetaFromJson(mj["catalog"]);
            catalogVisibleExplicit =
                mj["catalog"].contains("catalogVisible") &&
                mj["catalog"]["catalogVisible"].is_boolean();
            pickCountExplicit = mj["catalog"].contains("pickCount") &&
                                mj["catalog"]["pickCount"].is_number_integer();
        }
    }

    // (3) CONTRACT SURUMU — TEK ALAN: minContractVersion (CONTRACT.md §4.1).
    // Eski "contractVersion" anahtari KABUL EDILMEZ: iki ad kafa karisikligi
    // uretiyordu (biri yaziyor, digeri okuyor). Paketler minContractVersion yazar.
    {
        std::string ver;
        if (mj.contains("contractVersion"))
            return fail(out, L"manifest 'contractVersion' kullanmis; alan adi 'minContractVersion' olmali");
        if (mj.contains("minContractVersion") && mj["minContractVersion"].is_string())
            ver = mj["minContractVersion"].get<std::string>();
        else
            return fail(out, L"manifest minContractVersion bildirmiyor");
        int a = 0, b = 0, c = 0;
        if (!parseSemver(ver, a, b, c))
            return fail(out, L"manifest surumu semver degil: " + wideAscii(ver));
        if (!contractSupports(a, b))
            return fail(out, L"paket daha yeni bir sozlesme istiyor: " + wideAscii(ver) +
                                  L" (mevcut " + wideAscii(kContractVersion) + L")");
        // manifest.previewInput (OPSIYONEL): parca secilmeden acilan diyalogda
        // temsili 3B onizleme girdisi (CP-01 8.2). Yoksa tests/*.json ilk input.
        if (mj.contains("previewInput")) {
            if (!mj["previewInput"].is_object())
                return fail(out, L"manifest.previewInput nesne degil");
            out.dialog.previewInput = componentInputFromJson(mj["previewInput"]);
        }
    }

    // (4) PARAMETERS
    std::vector<CompParamSpec> specs;
    {
        json pj;
        std::wstring err;
        if (!readJsonFile(dir / kPackageParametersFile, pj, err)) return fail(out, err);
        const json* list = nullptr;
        if (pj.is_array()) list = &pj;
        else if (pj.is_object() && pj.contains("parameters") && pj["parameters"].is_array())
            list = &pj["parameters"];
        if (!list) return fail(out, L"parameters.json: parametre dizisi yok");
        for (const auto& s : *list) {
            if (!s.is_object()) return fail(out, L"parameters.json: oge nesne degil");
            CompParamSpec spec = compParamSpecFromJson(s);
            if (spec.name.empty())
                return fail(out, L"parameters.json: ad alani bos parametre");
            for (const auto& prev : specs)
                if (prev.name == spec.name)
                    return fail(out, L"parameters.json: tekil olmayan parametre adi: " + spec.name);
            specs.push_back(std::move(spec));
        }
        if (specs.empty()) return fail(out, L"paket parametre semasi bos");
        out.type.params = specs;
    }

    // (5) DIALOG (validateDialogSchema)
    {
        json dj;
        std::wstring err;
        if (!readJsonFile(dir / kPackageDialogFile, dj, err)) return fail(out, err);
        ComponentDialogSchema schema = componentDialogSchemaFromJson(dj);
        // §Sema: schema.typeId ve definition.typeId AYRI alanlar; IKISI de dolu olmali.
        if (schema.typeId.empty()) schema.typeId = out.typeId;
        if (schema.typeId != out.typeId)
            return fail(out, L"dialog.json typeId manifest ile uyusmuyor: " + schema.typeId);
        const DialogSchemaValidation v = validateDialogSchema(schema, specs);
        if (!v.ok) {
            std::wstring msg = L"dialog semasi gecersiz";
            for (const auto& p : v.problems) msg += L"; " + p;
            return fail(out, msg);
        }
        // MM-6: onizleme tanimi da FAIL-CLOSED dogrulanir — olcu parametresi semada
        // yoksa ya da axis/rol bicimi bozuksa tip kaydedilmez.
        for (const auto& dim : schema.preview.dimensions) {
            bool known = false;
            for (const auto& spec : specs)
                if (spec.name == dim.param) { known = true; break; }
            if (!known)
                return fail(out, L"preview: bilinmeyen parametre: " + dim.param);
            if (dim.axis != L'u' && dim.axis != L'v' && dim.axis != L'w')
                return fail(out, L"preview: axis u|v|w olmali (" + dim.param + L")");
            if (dim.from.find(L'.') == std::wstring::npos ||
                dim.to.find(L'.') == std::wstring::npos)
                return fail(out, L"preview: from/to '<rol>.<anchor>' biciminde olmali (" +
                                      dim.param + L")");
        }
        for (const auto& tg : schema.preview.toggles) {
            const CompParamSpec* spec = nullptr;
            for (const auto& s : specs)
                if (s.name == tg.param) { spec = &s; break; }
            if (!spec)
                return fail(out, L"preview toggle: bilinmeyen parametre: " + tg.param);
            if (spec->type != CompParamType::Boolean)
                return fail(out, L"preview toggle: parametre Boolean olmali: " + tg.param);
        }
        out.dialog.typeId = out.typeId;
        out.dialog.specs = specs;
        out.dialog.schema = std::move(schema);
    }

    // (6) RECIPE (statik analiz)
    {
        json rj;
        std::wstring err;
        if (!readJsonFile(dir / kPackageRecipeFile, rj, err)) return fail(out, err);
        // FAIL-CLOSED (ST-3g): (1) recipeVersion 1.x olmali, (2) argumanlar
        // metin/sayi ifade olmali — JSON boolean "1"/"0" metnine CEVİRİLMEZ.
        {
            std::wstring rerr;
            if (!validateRecipeJsonHeader(rj, rerr)) return fail(out, rerr);
        }
        out.recipe = recipeFromJson(rj);
        if (out.recipe.steps.empty())
            return fail(out, L"recipe.json: adim yok");
        const RecipeValidation rv = validateRecipe(out.recipe, specs);
        if (!rv.ok) return fail(out, L"recipe gecersiz: " + rv.message);

        // (6b) KATALOG VARSAYILANLARI (P-1/4).
        //  - catalogVisible verilmediyse: paket katalogda GORUNUR (aksi hâlde
        //    Studio paketi MM katalogunda ve aramasinda hic gorunmuyordu).
        //  - pickCount verilmediyse: recipe'nin kullandigi uye sayisindan
        //    cikarilir (tek uyeli taban levhasi paketleri "tam 2 parca secin"
        //    ile reddediliyordu).
        if (!catalogVisibleExplicit) out.type.catalog.catalogVisible = true;
        if (!pickCountExplicit) {
            const int inferred = inferPickCountFromRecipeJson(rj);
            out.type.catalog.pickCount = inferred;
            out.warnings.push_back(L"manifest.catalog.pickCount yok — recipe'den cikarildi: " +
                                   std::to_wstring(inferred));
        }
        // Secim istemleri: manifest tur vermediyse notr Turkce metin.
        if (out.type.catalog.primaryMemberType.empty())
            out.type.catalog.primaryMemberType = out.type.catalog.pickCount == 1 ? L"üye" : L"ana üye";
        if (out.type.catalog.pickCount > 1 && out.type.catalog.secondaryMemberType.empty())
            out.type.catalog.secondaryMemberType = L"ikincil üye";
    }

    // (7) ALTIN TESTLER — yapisal dogrulama + DIGEST REPLAY.
    //
    // Test dosyasi bicimi (tests/<ad>.json):
    //   { "name": "...", "input": <ComponentInput JSON>,
    //     "expect": { "planDigest": "<sha256 hex>" } }
    // "input" varsa recipe BU girdiyle kosulur ve planDigest karsilastirilir
    // (golden replay). Girdi yoksa yalniz yapisal dogrulama yapilir.
    {
        const fs::path td = dir / kPackageTestsDir;
        if (fs::is_directory(td, ec)) {
            std::size_t replayed = 0, structuralOnly = 0;
            std::optional<ComponentInput> representativeInput;
            for (const auto& e : fs::directory_iterator(td, ec)) {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".json") continue;
                json tj;
                std::wstring err;
                if (!readJsonFile(e.path(), tj, err))
                    return fail(out, L"altin test okunamadi: " + err);
                if (!tj.is_object() || !tj.contains("name") || !tj["name"].is_string())
                    return fail(out, L"altin test 'name' alani yok: " + pathText(e.path().filename()));
                if (tj.contains("expect")) {
                    const json& ex = tj["expect"];
                    if (!ex.is_object())
                        return fail(out, L"altin test 'expect' nesne degil: " +
                                              pathText(e.path().filename()));
                    if (ex.contains("planDigest") && !ex["planDigest"].is_string())
                        return fail(out, L"altin test planDigest string degil: " +
                                              pathText(e.path().filename()));
                }
                if (!tj.contains("input")) {
                    ++structuralOnly;
                    continue;
                }
                // TEMSILI ONIZLEME GIRDISI: ilk testin girdisi (dosya adi sirali
                // oldugu icin deterministik) — parca secilmeden acilan diyalog
                // 3B onizlemeyi bununla cizer (CP-01 8.2 / ST-4 GUI).
                if (!representativeInput.has_value()) {
                    const ComponentInput in = componentInputFromJson(tj["input"]);
                    representativeInput = in;
                }
                if (!tj["input"].is_object())
                    return fail(out, L"altin test 'input' nesne degil: " +
                                          pathText(e.path().filename()));
                const json& ex = tj.contains("expect") ? tj["expect"] : json::object();
                if (!ex.contains("planDigest") || !ex["planDigest"].is_string())
                    return fail(out, L"altin test girdi tasiyor ama planDigest yok: " +
                                          pathText(e.path().filename()));
                const ComponentInput in = componentInputFromJson(tj["input"]);
                // E (b): kanoniklik denetimi — elde kurulan (memberGeometryFromProfile'tan
                // gecmeyen) girdi acik mesajla REDDEDILIR; sessizce kanoniklestirilmez.
                {
                    std::wstring who;
                    if (!isCanonicalMemberGeometry(in.main))
                        who = L"main";
                    else if (in.secondary.empty() ||
                             !isCanonicalMemberGeometry(in.secondary.front()))
                        who = L"secondary";
                    if (!who.empty())
                        return fail(out, L"altin test girdisi KANONIK DEGIL (" + who +
                                          L"): " + pathText(e.path().filename()) +
                                          L" — memberGeometryFromProfile ile kurulmali");
                }
                const GenerationPlan plan = runRecipe(out.recipe, specs, in);
                if (!plan.ok)
                    return fail(out, L"altin test kosulamadi (" +
                                          pathText(e.path().filename()) + L"): " + plan.error);
                const std::string want = ex["planDigest"].get<std::string>();
                const std::string got = generationPlanDigest(plan);
                if (want != got)
                    return fail(out, L"altin test digest TUTMADI (" +
                                          pathText(e.path().filename()) + L"): beklenen " +
                                          pathText(want) + L", uretilen " + pathText(got));
                ++replayed;
            }
            if (replayed > 0)
                out.warnings.push_back(L"altin testler: " + std::to_wstring(replayed) +
                                       L" girdi replay edildi (planDigest eslesti)");
            if (structuralOnly > 0)
                out.warnings.push_back(L"altin testler: " + std::to_wstring(structuralOnly) +
                                       L" dosya yalniz yapisal dogrulandi (input yok)");
            // ONCELIK: manifest.previewInput > tests/*.json ilk input.
            if (!out.dialog.previewInput.has_value() && representativeInput.has_value())
                out.dialog.previewInput = *representativeInput;
        }
    }

    // (8) CAKISMA — yerlesik veya daha once yuklenmis typeId REDDEDILIR.
    // P-1/3 istisnasi: `allowExistingPackage` + tip PAKET kaynakli ise gec
    // (surum karsilastirmasi installComponentPackage'de yapilir).
    if (registry.findType(out.typeId) != nullptr &&
        !(allowExistingPackage && registry.isPackageType(out.typeId)))
        return fail(out, L"typeId zaten kayitli (yerlesik tip paketle EZILMEZ): " + out.typeId);

    out.ok = true;
    return out;
}

}  // namespace

ComponentPackageLoad loadComponentPackage(const fs::path& dir,
                                          const ComponentRegistry& registry) {
    return loadPackageImpl(dir, registry, /*allowExistingPackage=*/false);
}

// --- kok tarama --------------------------------------------------------------

ComponentPackageScan loadComponentPackages(ComponentRegistry& registry,
                                           ComponentDialogRegistry& dialogs,
                                           const std::vector<fs::path>& roots) {
    ComponentPackageScan report;
    std::set<std::wstring> seen;

    for (const auto& root : roots) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) continue;

        std::vector<fs::path> candidates;
        if (fs::is_regular_file(root / kPackageManifestFile, ec)) {
            candidates.push_back(root);  // kok dogrudan bir paket
        } else {
            for (const auto& e : fs::directory_iterator(root, ec)) {
                std::error_code ep;
                if (e.is_directory(ep) && fs::is_regular_file(e.path() / kPackageManifestFile, ep))
                    candidates.push_back(e.path());
            }
            std::sort(candidates.begin(), candidates.end());  // deterministik sira
        }

        for (const auto& dir : candidates) {
            // GOLGELEME: ayni typeId daha once (onceki kokte = proje klasoru)
            // yuklendiyse ikinci kopya DOGRULANMAZ bile; golgelenen olarak raporlanir.
            std::wstring candidateId;
            {
                json mj;
                std::wstring ignore;
                if (readJsonFile(dir / kPackageManifestFile, mj, ignore) && mj.is_object() &&
                    mj.contains("typeId") && mj["typeId"].is_string())
                    candidateId = wideFromUtf8(mj["typeId"].get<std::string>());
            }
            if (!candidateId.empty() && seen.count(candidateId)) {
                report.shadowed.push_back(candidateId + L" (" + pathText(dir) + L")");
                continue;
            }

            ComponentPackageLoad res = loadComponentPackage(dir, registry);
            if (!res.ok) {
                report.errors.push_back(pathText(dir) + L": " + res.message);
                continue;
            }
            if (seen.count(res.typeId)) {
                // Ayni typeId ikinci kokte: ILK kok (proje klasoru) kazanir.
                report.shadowed.push_back(res.typeId + L" (" + pathText(dir) + L")");
                continue;
            }
            if (!registry.registerType(std::move(res.type), makeRecipeGenerator(res.dialog, res.recipe),
                                       /*fromPackage=*/true)) {
                report.errors.push_back(pathText(dir) + L": registry kaydi reddedildi (" +
                                        res.typeId + L")");
                continue;
            }
            if (!dialogs.registerDialog(std::move(res.dialog))) {
                // "Kismi kayit yok" (component_package.hpp basligi): tip kaydi GERI
                // ALINIR, yoksa mesaj yalan soyler ve yarim kayit kalirdi (HR-10).
                registry.unregisterType(res.typeId);
                report.errors.push_back(pathText(dir) +
                                        L": dialog kaydi reddedildi, tip geri alindi (" +
                                        res.typeId + L")");
                continue;
            }
            seen.insert(res.typeId);
            report.registered.push_back(res.typeId);
            for (const auto& w : res.warnings)
                report.warnings.push_back(res.typeId + L": " + w);
        }
    }
    return report;
}

fs::path defaultUserComponentPackagesDir() {
    // Birincil: %APPDATA%. Yedek: %USERPROFILE%\AppData\Roaming — bazi
    // baslatma yollari (kisaltilmis ortam, servis/otomasyon) APPDATA'yi
    // gecirmiyor; o durumda paket kurulumu SESSIZCE bos klasore gitmesin.
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return fs::path(appdata) / "ModelMaker" / "components";
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
        return fs::path(profile) / "AppData" / "Roaming" / "ModelMaker" / "components";
    return {};
}

fs::path projectComponentPackagesDir(const fs::path& projectFileOrDir) {
    std::error_code ec;
    if (projectFileOrDir.empty()) return {};
    if (fs::is_regular_file(projectFileOrDir, ec))
        return projectFileOrDir.parent_path() / "components";
    return projectFileOrDir / "components";
}

// --- P-1/1..3: ATOMIK KURULUM -------------------------------------------------

namespace {

// Manifest'ten YALNIZ typeId okur (dogrulama degil; hedef klasor adi icin).
std::wstring peekTypeId(const fs::path& dir) {
    json mj;
    std::wstring ignore;
    if (!readJsonFile(dir / kPackageManifestFile, mj, ignore)) return {};
    if (!mj.is_object() || !mj.contains("typeId") || !mj["typeId"].is_string()) return {};
    return wideFromUtf8(mj["typeId"].get<std::string>());
}

// Tek dosya/klasor agacini kopyalar (paket duzdur, symlink izlenmez).
bool copyTree(const fs::path& from, const fs::path& to, std::wstring& err) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = L"kopyalama basarisiz: " + pathText(from) + L" -> " + pathText(to) +
              L" (" + wideAscii(ec.message()) + L")";
        return false;
    }
    return true;
}

ComponentPackageInstallResult installFail(ComponentPackageInstallResult out, std::wstring msg) {
    out.ok = false;
    out.message = std::move(msg);
    return out;
}

}  // namespace

bool isInstalledPackageType(const ComponentRegistry& registry, const std::wstring& typeId) {
    return registry.isPackageType(typeId);
}

ComponentPackageInstallResult installComponentPackage(
    const fs::path& sourceDir, const fs::path& targetRoot, ComponentRegistry& registry,
    ComponentDialogRegistry& dialogs, const ComponentPackageInstallOptions& options) {
    ComponentPackageInstallResult out;
    std::error_code ec;

    // (0) kaynak klasor
    if (sourceDir.empty() || !fs::is_directory(sourceDir, ec))
        return installFail(out, L"paket klasoru bulunamadi: " + pathText(sourceDir));

    // (1) typeId + mevcut kayit
    out.typeId = peekTypeId(sourceDir);
    if (out.typeId.empty())
        return installFail(out, L"manifest.typeId okunamadi (" + pathText(sourceDir) + L")");

    const ComponentType* existing = registry.findType(out.typeId);
    const bool existingIsPackage = existing != nullptr && registry.isPackageType(out.typeId);
    if (existing != nullptr && !existingIsPackage)
        return installFail(out, L"typeId yerlesik bir tiple cakisiyor, paket kurulmaz: " +
                                    out.typeId);

    // (2) paketi DOGRULA (henuz hicbir sey yazilmadi)
    ComponentPackageLoad load = loadPackageImpl(sourceDir, registry, existingIsPackage);
    if (!load.ok) return installFail(out, load.message);

    out.displayName = load.displayName;
    out.schemaVersion = load.type.schemaVersion;
    out.package = load;

    // (3) surum kurali (P-1/3)
    if (existing != nullptr) {
        out.replaced = true;
        out.previousSchemaVersion = existing->schemaVersion;
        if (load.type.schemaVersion > existing->schemaVersion) {
            out.upgraded = true;
        } else if (load.type.schemaVersion == existing->schemaVersion &&
                   options.allowIdenticalReinstall) {
            out.upgraded = false;
        } else {
            return installFail(out, L"ayni typeId icin surum yukseltilmemis: mevcut " +
                                        std::to_wstring(existing->schemaVersion) + L", paket " +
                                        std::to_wstring(load.type.schemaVersion) +
                                        L" (paket kurulmadi, mevcut kayit korundu)");
        }
    }

    // (4) hedef klasor + gecici kopya
    if (targetRoot.empty())
        return installFail(out, L"hedef klasor bos (APPDATA/USERPROFILE yok): paket "
                                    L"kullanici klasorune kurulamaz");
    const fs::path target = targetRoot / (out.typeId + L".mmcpkg");
    const fs::path tmp = targetRoot / (L"." + out.typeId + L".tmp");
    const fs::path old = targetRoot / (L"." + out.typeId + L".old");
    fs::create_directories(targetRoot, ec);
    if (ec)
        return installFail(out, L"hedef klasor olusturulamadi: " + pathText(targetRoot) +
                                    L" (" + wideAscii(ec.message()) + L")");
    fs::remove_all(tmp, ec);
    fs::remove_all(old, ec);

    std::wstring cerr;
    if (!copyTree(sourceDir, tmp, cerr)) {
        fs::remove_all(tmp, ec);
        return installFail(out, cerr);
    }
    // Kopyanin kendisi de dogrulanir: checksums DOSYA UZERINDEN tutmali.
    {
        ComponentPackageLoad verify = loadPackageImpl(tmp, registry, true);
        if (!verify.ok) {
            fs::remove_all(tmp, ec);
            return installFail(out, L"kopya dogrulanamadi: " + verify.message);
        }
    }

    // (5) registry: eski kayit (varsa) saklanir, yenisi yazilir
    ComponentType oldType;
    ComponentDialogDefinition oldDialog;
    ComponentGenerator oldGen;
    bool hadOld = false;
    if (existing != nullptr) {
        oldType = *existing;
        if (const ComponentDialogDefinition* d = dialogs.find(out.typeId)) oldDialog = *d;
        if (const ComponentGenerator* g = registry.generator(out.typeId)) oldGen = *g;
        hadOld = true;
        registry.unregisterType(out.typeId);
        dialogs.unregisterDialog(out.typeId);
    }
    const bool typeOk = registry.registerType(load.type, makeRecipeGenerator(load.dialog, load.recipe),
                                             /*fromPackage=*/true);
    const bool dialogOk = typeOk && dialogs.registerDialog(load.dialog);
    if (!typeOk || !dialogOk) {
        if (typeOk) registry.unregisterType(out.typeId);
        if (dialogOk) dialogs.unregisterDialog(out.typeId);
        if (hadOld) {
            registry.registerType(oldType, oldGen, /*fromPackage=*/true);
            dialogs.registerDialog(oldDialog);
        }
        fs::remove_all(tmp, ec);
        return installFail(out, L"registry kaydi reddedildi, paket kurulmadi: " + out.typeId);
    }

    // (6) ATOMIK yer degistirme: eski -> .old, gecici -> hedef, .old silinir
    auto restoreRegistry = [&]() {
        registry.unregisterType(out.typeId);
        dialogs.unregisterDialog(out.typeId);
        if (hadOld) {
            registry.registerType(oldType, oldGen, /*fromPackage=*/true);
            dialogs.registerDialog(oldDialog);
        }
    };
    const bool hadTarget = fs::exists(target, ec);
    if (hadTarget) {
        fs::rename(target, old, ec);
        if (ec) {
            restoreRegistry();
            fs::remove_all(tmp, ec);
            return installFail(out, L"mevcut paket yerinden alinamadi: " + pathText(target));
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        if (hadTarget) {
            std::error_code er;
            fs::rename(old, target, er);
        }
        restoreRegistry();
        fs::remove_all(tmp, ec);
        return installFail(out, L"paket yerine konulamadi: " + pathText(target));
    }
    if (hadTarget) fs::remove_all(old, ec);

    out.ok = true;
    out.targetDir = target;
    if (out.replaced)
        out.message = L"paket guncellendi (surum " + std::to_wstring(out.previousSchemaVersion) +
                      L" -> " + std::to_wstring(out.schemaVersion) + L"): " + out.displayName;
    else
        out.message = L"paket kuruldu: " + out.displayName + L" (" + out.typeId + L")";
    return out;
}


}  // namespace mm
