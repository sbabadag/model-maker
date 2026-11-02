// MM-5 — PAKET YUKLEYICI testi (OCC GEREKTIRMEZ, Linux'ta kosar).
//
// Kabul (COMPONENT-PACKAGE-01 §4.4 + MM-5 madde 4):
//   - packages/end_plate yerlesik typeId ile CAKISIR -> RED (yerlesik tip ezilmez)
//   - ayni paket "mm_end_plate_pkg" typeId'siyle yuklenir -> tip + dialog kaydolur,
//     registry.generate() YERLESIK generateEndPlate ile AYNI plani uretir (digest)
//   - bozuk paketler (checksum, sema, bilinmeyen tas, yeni sozlesme, gecersiz
//     typeId) YUKLENMEZ; basarisiz yukleme registry'de IZ BIRAKMAZ
//   - proje klasoru ayni typeId'yi GOLGELER
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/sha256.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mm;
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

// source paketi hedef klasore kopyalar; manifest typeId'sini degistirir.
static bool makePackageCopy(const fs::path& src, const fs::path& dst,
                            const std::string& newTypeId) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (const char* f : kCoreFiles) {
        if (!fs::is_regular_file(src / f, ec)) return false;
        if (!writeAll(dst / f, readAll(src / f))) return false;
    }
    json mj;
    try {
        mj = json::parse(readAll(dst / "manifest.json"));
    } catch (...) {
        return false;
    }
    mj["typeId"] = newTypeId;
    mj["minContractVersion"] = "1.0.0";
    if (!writeAll(dst / "manifest.json", mj.dump(2))) return false;
    // dialog.json typeId'sini de hizala (yukleyici ikisinin esitligini ister).
    json dj;
    try {
        dj = json::parse(readAll(dst / "dialog.json"));
    } catch (...) {
        return false;
    }
    dj["typeId"] = newTypeId;
    return writeAll(dst / "dialog.json", dj.dump(2));
}

static bool writeChecksums(const fs::path& dir) {
    json cj = json::object();
    for (const char* f : kCoreFiles) {
        const std::string bytes = readAll(dir / f);
        if (bytes.empty()) return false;
        cj[f] = sha256Hex(bytes);
    }
    return writeAll(dir / "checksums.json", cj.dump(2));
}

static ComponentInput makeCase0() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = Vec3{0.0, 0.0, 0.0};
    in.main.axisTo = Vec3{0.0, 0.0, 3000.0};
    in.main.refNormal = Vec3{0.0, 0.0, 1.0};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;

    MemberGeometry b;
    b.id = 2;
    b.axisFrom = Vec3{0.0, 0.0, 3000.0};
    b.axisTo = Vec3{2000.0, 0.0, 3000.0};
    b.refNormal = Vec3{0.0, 0.0, 1.0};
    b.secWidth = 200.0;
    b.secHeight = 400.0;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary.push_back(b);
    return in;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "mm_pkg_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const fs::path endPlatePkg = kPackages / "end_plate";
    check(fs::is_regular_file(endPlatePkg / "manifest.json", ec), "0 fixture paketi mevcut");

    // 1) Yerlesik typeId ile cakisma -> RED (yerlesik tip ezilmez).
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentDialogRegistry dialogs;
        registerBuiltInDialogs(dialogs);
        const ComponentPackageLoad r = loadComponentPackage(endPlatePkg, reg);
        check(!r.ok, "1 yerlesik typeId'li paket REDDEDILIR");
        check(r.message.find(L"zaten kayitli") != std::wstring::npos,
              "1 ret mesaji cakismayi adlandirir");
        check(reg.findType(L"beam_to_column_end_plate") != nullptr,
              "1 yerlesik tip kayitli kalir (ezilmedi)");
    }

    // 2) mm_end_plate_pkg olarak yukle -> tip + dialog + recipe kaydi.
    const fs::path pkgA = root / "proje" / "components" / "end_plate_pkg";
    check(makePackageCopy(endPlatePkg, pkgA, "mm_end_plate_pkg"), "2 paket kopyasi olusturuldu");
    check(writeChecksums(pkgA), "2 checksums.json yazildi");
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentDialogRegistry dialogs;
        registerBuiltInDialogs(dialogs);

        const ComponentPackageLoad r = loadComponentPackage(pkgA, reg);
        check(r.ok, "2 paket dogrulandi (ok)");
        if (!r.ok) std::printf("   yukleme hatasi: %ls\n", r.message.c_str());
        check(r.type.params.size() > 0, "2 parametre semasi yuklendi");
        check(r.dialog.typeId == L"mm_end_plate_pkg", "2 dialog typeId manifest ile ayni");
        check(!r.dialog.schema.groups.empty() || !r.dialog.schema.items.empty(),
              "2 sematik sema yuklendi");
        check(r.recipe.steps.size() == 1 && r.recipe.steps[0].use == "endplate.placement",
              "2 recipe yuklendi");

        ComponentType type = r.type;
        ComponentDialogDefinition dialog = r.dialog;
        const bool reg1 = reg.registerType(std::move(type), makeRecipeGenerator(dialog, r.recipe));
        const bool reg2 = dialogs.registerDialog(std::move(dialog));
        check(reg1 && reg2, "2 tip ve dialog registry'ye kaydedildi");

        // Kabul: yuklenen paket, yerlesik generator ile AYNI plani uretir.
        const ComponentInput in = makeCase0();
        const GenerationPlan pkgPlan = reg.generate(L"mm_end_plate_pkg", in);
        const GenerationPlan builtinPlan = reg.generate(L"beam_to_column_end_plate", in);
        check(pkgPlan.ok, "2 paket ureteci plan uretir");
        check(pkgPlan.ok && builtinPlan.ok &&
                  generationPlanDigest(pkgPlan) == generationPlanDigest(builtinPlan),
              "2 paket plani == yerlesik end plate plani (digest)");
        check(pkgPlan.plates.size() == builtinPlan.plates.size() &&
                  pkgPlan.boltGroups.size() == builtinPlan.boltGroups.size(),
              "2 levha/bulon sayilari ayni");
        check(reg.findType(L"mm_end_plate_pkg") != nullptr &&
                  reg.findType(L"beam_to_column_end_plate") != nullptr,
              "2 iki tip bir arada yasar (biri digerini ezmez)");
    }

    // 2b) DEPODA GONDERILEN fixture (Windows kabulu: %APPDATA%'ya kopyala-yukle).
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(kPackages / "end_plate_pkg", reg);
        check(r.ok && r.typeId == L"mm_end_plate_pkg", "2b depodaki fixture paketi yuklenir");
        check(r.ok && r.type.catalog.catalogVisible,
              "2b fixture katalogda gorunur (generic komut tipi secer)");
    }

    // 3) Bozuk paketler YUKLENMEZ ve registry'de iz birakmaz.
    auto expectReject = [](const fs::path& dir, const wchar_t* needle, const char* label) {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(dir, reg);
        check(!r.ok, label);
        if (!r.ok && needle)
            check(r.message.find(needle) != std::wstring::npos, label);
        check(reg.findType(L"mm_broken_pkg") == nullptr, label);
    };

    // 3a) checksum bozulmus
    {
        const fs::path d = root / "bozuk_checksum";
        makePackageCopy(endPlatePkg, d, "mm_broken_pkg");
        writeChecksums(d);
        std::string bad = readAll(d / "recipe.json");
        bad += "\n";
        writeAll(d / "recipe.json", bad);
        expectReject(d, L"checksum", "3a checksum uyusmazligi REDDEDILIR");
    }

    // 3b) sema bozuk: ayni adli iki parametre
    {
        const fs::path d = root / "bozuk_sema";
        makePackageCopy(endPlatePkg, d, "mm_broken_pkg");
        json pj = json::parse(readAll(d / "parameters.json"));
        if (pj.contains("parameters") && pj["parameters"].is_array()) {
            json copy = pj["parameters"][0];
            pj["parameters"].push_back(copy);
        }
        writeAll(d / "parameters.json", pj.dump(2));
        writeChecksums(d);
        expectReject(d, L"tekil olmayan", "3b tekrarlanan parametre adi REDDEDILIR");
    }

    // 3c) bilinmeyen tas (recipe)
    {
        const fs::path d = root / "bozuk_recipe";
        makePackageCopy(endPlatePkg, d, "mm_broken_pkg");
        json rj = json::parse(readAll(d / "recipe.json"));
        rj["steps"][0]["use"] = "bilinmeyen.tas";
        writeAll(d / "recipe.json", rj.dump(2));
        writeChecksums(d);
        expectReject(d, L"recipe", "3c bilinmeyen tas REDDEDILIR");
    }

    // 3d) sozlesme surumu cok yeni
    {
        const fs::path d = root / "bozuk_sozlesme";
        makePackageCopy(endPlatePkg, d, "mm_broken_pkg");
        json mj = json::parse(readAll(d / "manifest.json"));
        mj["minContractVersion"] = "2.0.0";
        writeAll(d / "manifest.json", mj.dump(2));
        writeChecksums(d);
        expectReject(d, L"sozlesme", "3d yeni sozlesme isteyen paket REDDEDILIR");
    }

    // 3e) gecersiz typeId
    {
        const fs::path d = root / "bozuk_typeid";
        makePackageCopy(endPlatePkg, d, "MM-Bad");
        writeChecksums(d);
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(d, reg);
        check(!r.ok, "3e gecersiz typeId REDDEDILIR");
    }

    // 3f) checksums.json yok -> yuklenir ama UYARI raporlanir (eski paket bicimi).
    {
        const fs::path d = root / "checksumsuz";
        makePackageCopy(endPlatePkg, d, "mm_end_plate_pkg");
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(d, reg);
        check(r.ok, "3f checksums.json olmayan paket yuklenir (uyari ile)");
        bool warned = false;
        for (const auto& w : r.warnings)
            if (w.find(L"checksums.json yok") != std::wstring::npos) warned = true;
        check(warned, "3f uyari raporlanir");
    }

    // 4) Proje klasoru kullanici klasorunu GOLGELER (ayni typeId).
    {
        const fs::path projectRoot = root / "shadow" / "proje" / "components";
        const fs::path userRoot = root / "shadow" / "appdata" / "ModelMaker" / "components";
        const fs::path dp = projectRoot / "ep_proj";
        const fs::path du = userRoot / "ep_user";
        makePackageCopy(endPlatePkg, dp, "mm_shadow_pkg");
        writeChecksums(dp);
        makePackageCopy(endPlatePkg, du, "mm_shadow_pkg");
        {
            json mj = json::parse(readAll(du / "manifest.json"));
            mj["displayName"] = "Kullanici kopyasi";
            writeAll(du / "manifest.json", mj.dump(2));
        }
        writeChecksums(du);

        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentDialogRegistry dialogs;
        registerBuiltInDialogs(dialogs);
        const ComponentPackageScan scan =
            loadComponentPackages(reg, dialogs, {projectRoot, userRoot});
        check(scan.registered.size() == 1 && scan.registered[0] == L"mm_shadow_pkg",
              "4 tek kayit (proje kopyasi)");
        check(scan.shadowed.size() == 1, "4 kullanici kopyasi golgelendi");
        const ComponentType* t = reg.findType(L"mm_shadow_pkg");
        check(t != nullptr && t->displayName != L"Kullanici kopyasi",
              "4 kazanann proje kopyasi");
    }

    fs::remove_all(root, ec);
    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
