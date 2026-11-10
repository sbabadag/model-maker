// P-1/4 — PAKET KATALOG GÖRÜNÜRLÜĞÜ + SEÇİM SAYISI
//
// Studio paketleri manifestte `catalog` bloğu YAZMIYOR (henuz). Bu yuzden:
//   - paket tipleri katalogda GORUNUR olmali (eskiden catalogVisible=false
//     varsayiliyordu ve Studio paketi MM katalogunda/aramasinda gorunmuyordu),
//   - pickCount manifestte yoksa RECIPE'deki uye referanslarindan cikarilmali
//     (tek uyeli taban levhasi paketleri "tam 2 parca secin" ile reddediliyordu),
//   - manifest ACIKCA verirse (catalogVisible / pickCount) o deger kazanir.
#include "model_maker/component_catalog.hpp"
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

#ifndef MM_FIXTURES_DIR
#error "MM_FIXTURES_DIR tanimli olmali (tests/fixtures dizini)"
#endif

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
    for (const char* f : {"manifest.json", "parameters.json", "dialog.json", "recipe.json"})
        cj[f] = mm::sha256Hex(readAll(dir / f));
    return writeAll(dir / "checksums.json", cj.dump(2));
}

// Kaynak paketi kopyalar, manifestte verilen alanlari degistirir, checksums tazeler.
static bool clonePackage(const fs::path& src, const fs::path& dst,
                         const json& manifestPatch) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (const char* f : {"manifest.json", "parameters.json", "dialog.json", "recipe.json"})
        if (!writeAll(dst / f, readAll(src / f))) return false;
    json mj = json::parse(readAll(dst / "manifest.json"));
    for (auto it = manifestPatch.begin(); it != manifestPatch.end(); ++it)
        mj[it.key()] = it.value();
    if (!writeAll(dst / "manifest.json", mj.dump(2))) return false;
    // dialog.json typeId'si manifest ile AYNI olmali (yukleyici denetliyor).
    if (mj.contains("typeId") && mj["typeId"].is_string()) {
        json dj = json::parse(readAll(dst / "dialog.json"));
        dj["typeId"] = mj["typeId"];
        if (!writeAll(dst / "dialog.json", dj.dump(2))) return false;
    }
    return writeChecksums(dst);
}

int main() {
    const fs::path fixtures = fs::path(MM_FIXTURES_DIR);
    const fs::path studio = fixtures / "studio_pkgs" / "mm_stiffened_base_plate.mmcpkg";
    const fs::path endPlate = fixtures / "p1_pkg";  // secondary_0 kullanan paket

    const fs::path base = fs::temp_directory_path() / "mm_pkg_catalog_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    check(fs::is_directory(studio, ec), "0 Studio taban levhasi paketi mevcut");
    check(fs::is_directory(endPlate, ec), "0 iki uyeli paket fixture mevcut");

    mm::ComponentRegistry reg;
    mm::registerBuiltInComponents(reg);
    mm::ComponentDialogRegistry dialogs;
    mm::registerBuiltInDialogs(dialogs);

    // 1) STUDIO PAKETI (catalog blogu YOK): gorunur + pickCount=1.
    {
        const mm::ComponentPackageLoad load = mm::loadComponentPackage(studio, reg);
        check(load.ok, "1 Studio paketi yuklendi");
        // loadComponentPackage YALNIZ dogrular; katalog icin kayit gerekir.
        if (load.ok)
            check(reg.registerType(load.type, mm::makeRecipeGenerator(load.dialog, load.recipe),
                                   /*fromPackage=*/true),
                  "1 Studio paketi registry'ye kaydedildi");
        if (load.ok) {
            check(load.type.catalog.catalogVisible, "1 catalog blogu yokken GORUNUR");
            check(load.type.catalog.pickCount == 1,
                  "1 pickCount recipe'den 1 cikarildi (tek uye: main)");
            check(load.type.catalog.primaryMemberType == L"üye",
                  "1 tek uyeli paket icin notr Turkce istem");
        }
    }

    // 2) IKI UYELI PAKET (secondary_0): pickCount=2.
    {
        const mm::ComponentPackageLoad load = mm::loadComponentPackage(endPlate, reg);
        check(load.ok, "2 iki uyeli paket yuklendi");
        if (load.ok) {
            check(load.type.catalog.pickCount == 2, "2 pickCount recipe'den 2 cikarildi");
            check(load.type.catalog.catalogVisible, "2 iki uyeli paket de gorunur");
            check(!load.type.catalog.secondaryMemberType.empty(),
                  "2 ikincil uye istemi bos degil");
        }
    }

    // 3) MANIFEST ACIKCA VERIRSE o deger kazanir: pickCount=2 + catalogVisible=false.
    {
        const fs::path dir = base / "explicit.mmcpkg";
        json patch;
        patch["typeId"] = "mm_p14_explicit";
        patch["displayName"] = "P1/4 Explicit Catalog";
        json cat;
        cat["pickCount"] = 2;
        cat["catalogVisible"] = false;
        cat["primaryMemberType"] = "kolon";
        cat["secondaryMemberType"] = "kiris";
        patch["catalog"] = cat;
        check(clonePackage(studio, dir, patch), "3 acik katalog meta paketi kuruldu");
        const mm::ComponentPackageLoad load = mm::loadComponentPackage(dir, reg);
        if (!load.ok) std::printf("      [teshis] yukleme hatasi: %ls", load.message.c_str());
        check(load.ok, "3 acik katalog meta paketi yuklendi");
        if (load.ok) {
            check(load.type.catalog.pickCount == 2, "3 manifest pickCount=2 KAZANDI");
            check(!load.type.catalog.catalogVisible, "3 manifest catalogVisible=false KAZANDI");
            check(load.type.catalog.primaryMemberType == L"kolon",
                  "3 manifest uye tipi KAZANDI");
        }
    }

    // 4) KATALOG: paket tipleri (gorunur olanlar) katalogda LISTELENIR; arama
    //    indeksi displayName ile bulur. Gizli olan (3) listede YOK.
    {
        // TESHIS: registry'de tipin katalog metasi ne durumda?
        if (const mm::ComponentType* t = reg.findType(L"mm_stiffened_base_plate")) {
            std::printf("      [teshis] reg: visible=%d pick=%d package=%d",
                        t->catalog.catalogVisible ? 1 : 0, t->catalog.pickCount,
                        reg.isPackageType(L"mm_stiffened_base_plate") ? 1 : 0);
        } else {
            std::printf("      [teshis] reg: tip YOK");
        }
        const std::vector<mm::CatalogEntry> cat = mm::buildComponentCatalog(reg, dialogs);
        check(mm::findCatalogEntry(cat, L"mm_stiffened_base_plate") != nullptr,
              "4 Studio paketi katalogda LISTELENDI");
        check(mm::findCatalogEntry(cat, L"mm_p14_explicit") == nullptr,
              "4 catalogVisible=false paket listede YOK");
        const mm::CatalogEntry* e = mm::findCatalogEntry(cat, L"mm_stiffened_base_plate");
        check(e != nullptr && e->pickCount == 1, "4 katalog kaydinda pickCount=1");
        check(e != nullptr && e->displayName == L"Rijitlestirilmis taban levhasi",
              "4 katalog kaydinda Turkce gorunen ad");
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
