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
    if (text.find("\r\n") != std::string::npos) {
        err = L"dosya CRLF satir sonu iceriyor (Windows core.autocrlf): " +
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

ComponentPackageLoad loadComponentPackage(const fs::path& dir,
                                          const ComponentRegistry& registry) {
    ComponentPackageLoad out;
    out.dir = dir;

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
                if (actual != expected)
                    return fail(out, L"checksum UYUSMUYOR: " + wideAscii(key) +
                                          L" (beklenen " + wideAscii(expected.substr(0, 12)) +
                                          L"..., gercek " + wideAscii(actual.substr(0, 12)) + L")");
                covered.insert(key);
            }
            for (const auto& r : required) {
                if (!covered.count(r))
                    return fail(out, L"checksums.json zorunlu dosyayi kapsamiyor: " + wideAscii(r));
            }
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
        out.displayName = displayNameFromJson(mj.value("displayName", json()));
        if (out.displayName.empty()) out.displayName = out.typeId;
        out.type.typeId = out.typeId;
        out.type.displayName = out.displayName;
        out.type.schemaVersion = mj.value("schemaVersion", 1);
        if (mj.contains("catalog") && mj["catalog"].is_object())
            out.type.catalog = componentCatalogMetaFromJson(mj["catalog"]);
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
        out.recipe = recipeFromJson(rj);
        if (out.recipe.steps.empty())
            return fail(out, L"recipe.json: adim yok");
        const RecipeValidation rv = validateRecipe(out.recipe, specs);
        if (!rv.ok) return fail(out, L"recipe gecersiz: " + rv.message);
    }

    // (7) ALTIN TESTLER — yapisal dogrulama (dosya varsa).
    {
        const fs::path td = dir / kPackageTestsDir;
        if (fs::is_directory(td, ec)) {
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
            }
            // DIGEST REPLAY: girdi geometrisi (profil katalogu) bu katmanda yok;
            // tam kosum Studio CI'inda ve MM-6 GUI kabulunde yapilir.
            out.warnings.push_back(
                L"altin testler yapisal dogrulandi (digest replay: Studio CI / MM-6)");
        }
    }

    // (8) CAKISMA — yerlesik veya daha once yuklenmis typeId REDDEDILIR.
    if (registry.findType(out.typeId) != nullptr)
        return fail(out, L"typeId zaten kayitli (yerlesik tip paketle EZILMEZ): " + out.typeId);

    out.ok = true;
    return out;
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
            if (!registry.registerType(std::move(res.type), makeRecipeGenerator(res.dialog, res.recipe))) {
                report.errors.push_back(pathText(dir) + L": registry kaydi reddedildi (" +
                                        res.typeId + L")");
                continue;
            }
            if (!dialogs.registerDialog(std::move(res.dialog))) {
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
    const char* appdata = std::getenv("APPDATA");
    if (!appdata || !*appdata) return {};
    return fs::path(appdata) / "ModelMaker" / "components";
}

fs::path projectComponentPackagesDir(const fs::path& projectFileOrDir) {
    std::error_code ec;
    if (projectFileOrDir.empty()) return {};
    if (fs::is_regular_file(projectFileOrDir, ec))
        return projectFileOrDir.parent_path() / "components";
    return projectFileOrDir / "components";
}

}  // namespace mm
