// P-1/1..3 — KOMPONENT PAKETI KURULUMU (atomik kopya + surum yukseltme)
//
// Kapsam (docs/agent/P-1-STUDIO-PAKETLERI.md):
//   1) gecerli paket kullanici/proje klasorune ATOMIK kurulur, registry'ye hemen
//      kaydolur (yeniden baslatma yok),
//   2) yerlesik typeId REDDEDILIR (yerlesik tip paketle ezilmez),
//   3) gecersiz pakette HICBIR SEY degismez (fail-closed, yarim klasor yok),
//   4) ayni typeId + ayni/dusuk surum -> RED, hedefteki paket DOKUNULMAZ,
//   5) ayni typeId + daha yuksek schemaVersion -> KABUL (upgrade), hedefte yeni
//      icerik, registry yeni sema.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

#ifndef MM_PACKAGES_DIR
#error "MM_PACKAGES_DIR tanimli olmali (paket fixture dizini)"
#endif

static const fs::path kPackages = fs::path(MM_PACKAGES_DIR);
static const char* kCoreFiles[] = {"manifest.json", "parameters.json", "dialog.json",
                                   "recipe.json"};

static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool writeAll(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

static bool writeChecksums(const fs::path& dir) {
    json cj = json::object();
    for (const char* f : kCoreFiles) {
        const std::string bytes = readAll(dir / f);
        if (bytes.empty()) return false;
        cj[f] = mm::sha256Hex(bytes);
    }
    return writeAll(dir / "checksums.json", cj.dump(2));
}

// Kaynak paketi hedefe kopyalar; typeId ve schemaVersion degistirilebilir.
static bool makePackageCopy(const fs::path& src, const fs::path& dst,
                            const std::string& newTypeId, int schemaVersion = -1) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (const char* f : kCoreFiles) {
        if (!fs::is_regular_file(src / f, ec)) return false;
        if (!writeAll(dst / f, readAll(src / f))) return false;
    }
    json mj;
    json dj;
    try {
        mj = json::parse(readAll(dst / "manifest.json"));
        dj = json::parse(readAll(dst / "dialog.json"));
    } catch (...) {
        return false;
    }
    mj["typeId"] = newTypeId;
    mj["minContractVersion"] = std::string("1.0.0");
    if (schemaVersion > 0) mj["schemaVersion"] = schemaVersion;
    dj["typeId"] = newTypeId;
    if (!writeAll(dst / "manifest.json", mj.dump(2))) return false;
    if (!writeAll(dst / "dialog.json", dj.dump(2))) return false;
    return writeChecksums(dst);
}

static int countEntries(const fs::path& dir) {
    std::error_code ec;
    int n = 0;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) ++n;
    return n;
}

// Hedef kokte gercek paket klasoru disinda artik (.tmp/.old) kalmadi mi?
static bool noLeftovers(const fs::path& root, const std::string& typeId) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return true;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (name.find(".tmp") != std::string::npos) return false;
        if (name.find(".old") != std::string::npos) return false;
        // Yalniz beklenen paket klasoru kalmali.
        if (name != typeId + ".mmcpkg") return false;
    }
    return true;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "mm_pkg_install_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const fs::path srcPkg = kPackages / "end_plate";
    check(fs::is_regular_file(srcPkg / "manifest.json", ec), "0 fixture paketi mevcut");

    // Kaynak: yerlesik olmayan bir typeId'ye kopya (temiz paket).
    const fs::path srcClean = root / "src_clean";
    check(makePackageCopy(srcPkg, srcClean, "p1_demo_bracket", 1),
          "0 temiz kaynak paket hazirlandi");

    const fs::path userRoot = root / "user_components";

    // 1) Gecerli paket kurulur + registry'ye hemen kaydolur.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);

        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(srcClean, userRoot, reg, dialogs);
        check(r.ok, "1 gecerli paket kurulur");
        check(r.typeId == L"p1_demo_bracket", "1 typeId manifest'ten gelir");
        check(r.replaced == false, "1 ilk kurulumda replaced=false");
        check(r.upgraded == false, "1 ilk kurulumda upgraded=false");
        const fs::path target = userRoot / "p1_demo_bracket.mmcpkg";
        check(fs::is_directory(target, ec), "1 hedef klasor olustu");
        check(fs::is_regular_file(target / "recipe.json", ec),
              "1 paket dosyalari kopyalandi");
        check(noLeftovers(userRoot, "p1_demo_bracket"), "1 gecici klasor artigi yok");
        // Registry'ye HEMEN kayit (yeniden baslatma yok).
        check(reg.findType(L"p1_demo_bracket") != nullptr, "1 registry'de hemen gorunur");
        check(reg.isPackageType(L"p1_demo_bracket"), "1 tip PAKET kaynagi olarak isaretli");
        check(dialogs.find(L"p1_demo_bracket") != nullptr,
              "1 diyalog semasi da kaydedildi");
        check(!reg.isPackageType(L"beam_to_column_end_plate"),
              "1 yerlesik tip paket kaynagi DEGIL");
        check(r.message.find(L"kuruldu") != std::wstring::npos ||
                  r.message.find(L"yuklendi") != std::wstring::npos,
              "1 Turkce basari mesaji");
    }

    // 2) Yerlesik typeId -> RED, hicbir dosya yazilmaz.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path builtinPkg = root / "src_builtin";
        check(makePackageCopy(srcPkg, builtinPkg, "beam_to_column_end_plate", 99),
              "2 yerlesik typeId'li kaynak hazir");
        const fs::path userRoot2 = root / "user_builtin";
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(builtinPkg, userRoot2, reg, dialogs);
        check(!r.ok, "2 yerlesik typeId REDDEDILIR");
        check(r.message.find(L"yerlesik") != std::wstring::npos ||
                  r.message.find(L"zaten kayitli") != std::wstring::npos,
              "2 ret mesaji nedeni soyler");
        check(!fs::exists(userRoot2 / "beam_to_column_end_plate.mmcpkg", ec),
              "2 hicbir dosya yazilmadi");
        // Yerlesik tip bozulmadi.
        const mm::ComponentType* t = reg.findType(L"beam_to_column_end_plate");
        check(t != nullptr && t->schemaVersion != 99, "2 yerlesik tip EZILMEDI");
    }

    // 3) Bozuk paket (checksums uyusmuyor) -> RED, hedef dokunulmaz.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path badPkg = root / "src_bad";
        check(makePackageCopy(srcPkg, badPkg, "p1_bad_pkg", 1), "3 bozuk kaynak hazir");
        // Kopyalamadan SONRA recipe'yi boz: checksums artik uyusmaz.
        writeAll(badPkg / "recipe.json", "{\n  \"steps\": [ {\"op\": \"nope\"} ]\n}\n");
        const fs::path userRoot3 = root / "user_bad";
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(badPkg, userRoot3, reg, dialogs);
        check(!r.ok, "3 bozuk paket REDDEDILIR");
        check(!r.message.empty(), "3 Turkce teshis dolu");
        check(!fs::exists(userRoot3 / "p1_bad_pkg.mmcpkg", ec),
              "3 hedefe hicbir sey yazilmadi");
        check(reg.findType(L"p1_bad_pkg") == nullptr, "3 registry'ye kayit yok");
        check(noLeftovers(userRoot3, "p1_bad_pkg"), "3 gecici klasor artigi yok");
    }

    // 4) Ayni typeId + AYNI surum, farkli icerik -> RED; hedefteki paket korunur.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path userRoot4 = root / "user_samever";
        const mm::ComponentPackageInstallResult first =
            mm::installComponentPackage(srcClean, userRoot4, reg, dialogs);
        check(first.ok, "4 birinci kurulum basarili");
        const std::string before = readAll(userRoot4 / "p1_demo_bracket.mmcpkg" / "recipe.json");

        // Ayni surum, degisik displayName -> icerik farkli.
        const fs::path srcSame = root / "src_same";
        check(makePackageCopy(srcPkg, srcSame, "p1_demo_bracket", 1), "4 ayni surum kaynak");
        {
            json mj = json::parse(readAll(srcSame / "manifest.json"));
            mj["displayName"] = "P1 Demo Bracket v2";
            writeAll(srcSame / "manifest.json", mj.dump(2));
            writeChecksums(srcSame);
        }
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(srcSame, userRoot4, reg, dialogs);
        check(!r.ok, "4 ayni surum farkli icerik REDDEDILIR");
        check(r.message.find(L"surum") != std::wstring::npos ||
                  r.message.find(L"sürüm") != std::wstring::npos,
              "4 ret mesaji surumu adlandirir");
        check(readAll(userRoot4 / "p1_demo_bracket.mmcpkg" / "recipe.json") == before,
              "4 hedefteki paket DOKUNULMADI");
        const mm::ComponentType* t = reg.findType(L"p1_demo_bracket");
        check(t != nullptr && t->displayName != L"P1 Demo Bracket v2",
              "4 registry eski kayitta kaldi");
    }

    // 5) Ayni typeId + DAHA YUKSEK schemaVersion -> kabul (upgrade).
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path userRoot5 = root / "user_upgrade";
        const mm::ComponentPackageInstallResult first =
            mm::installComponentPackage(srcClean, userRoot5, reg, dialogs);
        check(first.ok && first.schemaVersion == 1, "5 v1 kuruldu");

        const fs::path srcV2 = root / "src_v2";
        check(makePackageCopy(srcPkg, srcV2, "p1_demo_bracket", 2), "5 v2 kaynak hazir");
        {
            json mj = json::parse(readAll(srcV2 / "manifest.json"));
            mj["displayName"] = "P1 Demo Bracket v2";
            writeAll(srcV2 / "manifest.json", mj.dump(2));
            writeChecksums(srcV2);
        }
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(srcV2, userRoot5, reg, dialogs);
        check(r.ok, "5 yuksek surum KABUL edilir");
        check(r.replaced, "5 replaced=true (onceki paket vardi)");
        check(r.upgraded, "5 upgraded=true");
        check(r.previousSchemaVersion == 1 && r.schemaVersion == 2, "5 surumler raporda");
        const mm::ComponentType* t = reg.findType(L"p1_demo_bracket");
        check(t != nullptr && t->schemaVersion == 2, "5 registry yeni surume guncellendi");
        check(t != nullptr && t->displayName == L"P1 Demo Bracket v2",
              "5 registry yeni icerigi tasiyor");
        check(noLeftovers(userRoot5, "p1_demo_bracket"), "5 upgrade sonrasi artik yok");
        check(fs::is_regular_file(userRoot5 / "p1_demo_bracket.mmcpkg" / "manifest.json", ec),
              "5 hedef klasoru saglam");
    }

    // 6) Ayni typeId + DAHA DUSUK surum -> RED.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path userRoot6 = root / "user_downgrade";
        const fs::path srcV2 = root / "src_v2b";
        check(makePackageCopy(srcPkg, srcV2, "p1_demo_bracket", 2), "6 v2 kaynak hazir");
        check(mm::installComponentPackage(srcV2, userRoot6, reg, dialogs).ok, "6 v2 kuruldu");
        const fs::path srcV1 = root / "src_v1b";
        check(makePackageCopy(srcPkg, srcV1, "p1_demo_bracket", 1), "6 v1 kaynak hazir");
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(srcV1, userRoot6, reg, dialogs);
        check(!r.ok, "6 dusuk surum REDDEDILIR");
        const mm::ComponentType* t = reg.findType(L"p1_demo_bracket");
        check(t != nullptr && t->schemaVersion == 2, "6 registry v2'de kaldi");
    }

    // 7) Proje klasorune gomme: ayni paket ikinci kokte de kurulabilir.
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const fs::path projectRoot = root / "project" / "components";
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(srcClean, projectRoot, reg, dialogs);
        check(r.ok, "7 proje klasorune gomme basarili");
        check(fs::is_regular_file(projectRoot / "p1_demo_bracket.mmcpkg" / "manifest.json", ec),
              "7 proje klasorunde paket var");
        check(noLeftovers(projectRoot, "p1_demo_bracket"), "7 proje klasorunde artik yok");
    }

    // 8) Kaynak klasor yok -> RED (cokme yok).
    {
        mm::ComponentRegistry reg;
        mm::registerBuiltInComponents(reg);
        mm::ComponentDialogRegistry dialogs;
        mm::registerBuiltInDialogs(dialogs);
        const mm::ComponentPackageInstallResult r =
            mm::installComponentPackage(root / "does_not_exist", root / "user_none", reg, dialogs);
        check(!r.ok, "8 olmayan kaynak REDDEDILIR");
        check(!r.message.empty(), "8 teshis dolu");
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
