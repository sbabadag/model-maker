// P-1/2 — YENIDEN YUKLEME POLITIKASI (kutuphane duzeyi)
//
// Application::reloadComponentPackages() su kurala dayanir:
//   1) PAKET kaynakli tipler kaldirilir, YERLESIK tipler korunur,
//   2) kokler GELISTIRME -> kullanici -> proje sirasiyla taranir: ilk kok
//      (gelistirme) ayni typeId'yi GOLGELER,
//   3) ikinci taramada ust surum (v2) kazanir, sonra v1'e donulur.
// Bu test Qt/OCCT/Document gerektirmez: yalniz registry + dialog + yukleyici.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

static const char* kCoreFiles[] = {"manifest.json", "parameters.json", "dialog.json",
                                   "recipe.json"};

static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool writeAll(const fs::path& p, const std::string& t) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << t;
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

// Kaynak paketi `dst` altina <typeId>.mmcpkg olarak yazar (surum/ad degisir).
static bool makePackage(const fs::path& src, const fs::path& dstRoot,
                        const std::string& typeId, int schemaVersion,
                        const std::string& displayName) {
    const fs::path dst = dstRoot / (typeId + ".mmcpkg");
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (const char* f : kCoreFiles) {
        if (!writeAll(dst / f, readAll(src / f))) return false;
    }
    json mj = json::parse(readAll(dst / "manifest.json"));
    json dj = json::parse(readAll(dst / "dialog.json"));
    mj["typeId"] = typeId;
    mj["schemaVersion"] = schemaVersion;
    mj["displayName"] = displayName;
    mj["minContractVersion"] = std::string("1.0.0");
    dj["typeId"] = typeId;
    if (!writeAll(dst / "manifest.json", mj.dump(2))) return false;
    if (!writeAll(dst / "dialog.json", dj.dump(2))) return false;
    return writeChecksums(dst);
}

// Application::reloadComponentPackages()'in (1)+(2) adimini birebir uygular.
static mm::ComponentPackageScan reloadLike(mm::ComponentRegistry& reg,
                                           mm::ComponentDialogRegistry& dialogs,
                                           const std::vector<fs::path>& roots) {
    std::vector<std::wstring> olds;
    for (const std::wstring& id : reg.typeIds())
        if (reg.isPackageType(id)) olds.push_back(id);
    for (const std::wstring& id : olds) {
        reg.unregisterType(id);
        dialogs.unregisterDialog(id);
    }
    return mm::loadComponentPackages(reg, dialogs, roots);
}

int main() {
    const fs::path base = fs::temp_directory_path() / "mm_pkg_reload_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    const fs::path src = fs::path(MM_PACKAGES_DIR) / "end_plate";
    const fs::path userRoot = base / "user";
    const fs::path devRoot = base / "dev";
    check(fs::is_regular_file(src / "manifest.json", ec), "0 fixture paketi mevcut");
    check(makePackage(src, userRoot, "p2_reload_demo", 1, "P2 Reload Demo v1"),
          "0 kullanici kokunde v1 paket");
    check(makePackage(src, devRoot, "p2_reload_demo", 2, "P2 Reload Demo v2"),
          "0 gelistirme kokunde v2 paket");

    mm::ComponentRegistry reg;
    mm::registerBuiltInComponents(reg);
    mm::ComponentDialogRegistry dialogs;
    mm::registerBuiltInDialogs(dialogs);
    const std::size_t builtinCount = reg.typeCount();

    // 1) Ilk yukleme: yalniz kullanici koku -> v1.
    mm::ComponentPackageScan scan = mm::loadComponentPackages(reg, dialogs, {userRoot});
    check(scan.registered.size() == 1, "1 kullanici koku yuklendi");
    const mm::ComponentType* t = reg.findType(L"p2_reload_demo");
    check(t != nullptr && t->schemaVersion == 1, "1 kurulu surum v1");
    check(reg.isPackageType(L"p2_reload_demo"), "1 tip paket kaynagi");

    // 2) Yeniden yukleme: GELISTIRME koku once -> v2 (golgeleme).
    scan = reloadLike(reg, dialogs, {devRoot, userRoot});
    check(scan.registered.size() == 1, "2 tek tip kayitli (golgeleme)");
    check(!scan.shadowed.empty(), "2 ikinci kopya golgelendi olarak raporlandi");
    t = reg.findType(L"p2_reload_demo");
    check(t != nullptr && t->schemaVersion == 2, "2 gelistirme klasoru v2'yi kazandirdi");
    check(t != nullptr && t->displayName == L"P2 Reload Demo v2",
          "2 gorunen ad yeni paketten");
    // Yerlesik tipler KORUNDU.
    check(reg.findType(L"beam_to_column_end_plate") != nullptr,
          "2 yerlesik tip korundu");
    check(reg.typeCount() == builtinCount + 1, "2 yalniz paket tipi yenilendi");
    check(!reg.isPackageType(L"beam_to_column_end_plate"),
          "2 yerlesik tip paket kaynagi olarak ISARETLENMEDI");

    // 3) Gelistirme koku kaldirildi -> v1'e donus.
    scan = reloadLike(reg, dialogs, {userRoot});
    check(scan.registered.size() == 1, "3 geri donuste tek tip");
    t = reg.findType(L"p2_reload_demo");
    check(t != nullptr && t->schemaVersion == 1, "3 v1'e dondu");
    check(dialogs.find(L"p2_reload_demo") != nullptr, "3 diyalog semasi da yenilendi");

    // 4) Bozuk (yarim yazilmis) gelistirme paketi: eski kayit KORUNUR, tip yok olmaz?
    //    Yukleyici fail-closed: bozuk paket ATLANIR. Yeniden yuklemede paket
    //    kaydi once kaldirildigi icin tip KAYBOLUR (dogru davranis: veri yok).
    {
        const fs::path badDev = base / "bad_dev";
        fs::create_directories(badDev / "p2_reload_demo.mmcpkg", ec);
        writeAll(badDev / "p2_reload_demo.mmcpkg" / "manifest.json", "{ \"typeId\": ");
        scan = reloadLike(reg, dialogs, {badDev, userRoot});
        check(scan.registered.size() == 1, "4 bozuk dev paketi atlandi, kullanici kopyasi yuklendi");
        check(!scan.errors.empty(), "4 bozuk paket hata olarak raporlandi");
        t = reg.findType(L"p2_reload_demo");
        check(t != nullptr && t->schemaVersion == 1, "4 gecerli paket kullanildi");
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
