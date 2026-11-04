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
        check(r.ok && r.dialog.schema.preview.dimensions.size() == 10,
              "2b fixture 10 olcu tanimi tasir (MM-6)");
        check(r.ok && !r.dialog.schema.preview.dimensions.empty() &&
                  r.dialog.schema.preview.dimensions[0].param == L"plateThickness",
              "2b ilk olcu plateThickness (kalinlik)");
    }

    // 2c) ALTIN TEST DIGEST REPLAY (ST-4/3): paketteki tests/*.json "input"
    //     alanlariyla recipe kosulur ve beklenen planDigest karsilastirilir.
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(kPackages / "end_plate_pkg", reg);
        bool replay = false;
        for (const auto& w : r.warnings)
            if (w.find(L"girdi replay edildi") != std::wstring::npos) replay = true;
        check(r.ok && replay, "2c altin testler girdi replay edildi (planDigest eslesti)");
    }

    // 2d) BOZUK planDigest -> paket REDDEDILIR (fail-closed, sessiz gecis yok).
    {
        const fs::path d = root / "mm_golden_broken";
        fs::remove_all(d, ec);
        fs::copy(kPackages / "end_plate_pkg", d, fs::copy_options::recursive, ec);
        check(!ec, "2d paket kopyalandi (tests dahil)");
        json mj = json::parse(readAll(d / "manifest.json"));
        mj["typeId"] = "mm_golden_broken";
        writeAll(d / "manifest.json", mj.dump(2));
        json dj = json::parse(readAll(d / "dialog.json"));
        dj["typeId"] = "mm_golden_broken";
        writeAll(d / "dialog.json", dj.dump(2));
        writeChecksums(d);
        json tj = json::parse(readAll(d / "tests" / "varsayilan.json"));
        tj["expect"]["planDigest"] = std::string(64, 'a');
        writeAll(d / "tests" / "varsayilan.json", tj.dump(2));
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(d, reg);
        check(!r.ok && r.message.find(L"digest TUTMADI") != std::wstring::npos,
              "2d bozuk planDigest REDDEDILIR (digest TUTMADI)");
    }

    // 2e) HR-10: dialog kaydi reddedilirse TIP GERI ALINIR (kismi kayit yok).
    {
        const fs::path d = root / "mm_rollback_pkg";
        fs::remove_all(d, ec);
        fs::copy(kPackages / "end_plate_pkg", d, fs::copy_options::recursive, ec);
        json mj = json::parse(readAll(d / "manifest.json"));
        mj["typeId"] = "mm_rollback_pkg";
        writeAll(d / "manifest.json", mj.dump(2));
        json dj = json::parse(readAll(d / "dialog.json"));
        dj["typeId"] = "mm_rollback_pkg";
        writeAll(d / "dialog.json", dj.dump(2));
        writeChecksums(d);

        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentDialogRegistry dialogs;
        // Gecerli dialog tanimini once paketten al, SONRA ayni typeId icin onceden
        // kaydet -> paketin dialog kaydi REDDEDILIR (rollback yolu).
        {
            ComponentRegistry scratch;
            registerBuiltInComponents(scratch);
            const ComponentPackageLoad pre = loadComponentPackage(d, scratch);
            check(pre.ok, "2e on yukleme (dialog tanimi alindi)");
            dialogs.registerDialog(pre.dialog);
        }

        const fs::path rootDir = root / "mm_rollback_root";
        fs::remove_all(rootDir, ec);
        fs::create_directories(rootDir, ec);
        fs::copy(d, rootDir / "mm_rollback_pkg", fs::copy_options::recursive, ec);
        const ComponentPackageScan scan = loadComponentPackages(reg, dialogs, {rootDir});
        check(!scan.errors.empty(), "2e dialog cakismasi hata olarak raporlanir");
        bool rollbackMsg = false;
        for (const auto& e : scan.errors)
            if (e.find(L"tip geri alindi") != std::wstring::npos) rollbackMsg = true;
        check(rollbackMsg, "2e mesaj 'tip geri alindi' der");
        check(reg.findType(L"mm_rollback_pkg") == nullptr,
              "2e HR-10: dialog reddedilince TIP GERI ALINDI (registry temiz)");
    }

    // 2f) HR-11: checksum uyusmazliginda dosyada CR varsa CRLF nedeni de yazilir.
    {
        const fs::path d = root / "mm_crlf_pkg";
        fs::remove_all(d, ec);
        fs::copy(kPackages / "end_plate_pkg", d, fs::copy_options::recursive, ec);
        json mj = json::parse(readAll(d / "manifest.json"));
        mj["typeId"] = "mm_crlf_pkg";
        writeAll(d / "manifest.json", mj.dump(2));
        json dj = json::parse(readAll(d / "dialog.json"));
        dj["typeId"] = "mm_crlf_pkg";
        writeAll(d / "dialog.json", dj.dump(2));
        writeChecksums(d);
        // recipe.json'i CRLF'e cevir (checksums BAYAT kalir -> uyusmazlik + CR bayti).
        std::string recipeText = readAll(d / "recipe.json");
        std::string crlf;
        for (char ch : recipeText) {
            if (ch == '\n') crlf.push_back('\r');
            crlf.push_back(ch);
        }
        writeAll(d / "recipe.json", crlf);
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(d, reg);
        check(!r.ok && r.message.find(L"checksum UYUSMUYOR") != std::wstring::npos,
              "2f CRLF: checksum uyusmazligi reddedilir");
        check(r.message.find(L"CR bayti") != std::wstring::npos,
              "2f HR-11: mesaj CRLF nedenini de soyler");
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

    // 3g) CRLF satir sonu (Windows autocrlf) -> ACIK ret mesaji.
    {
        const fs::path d = root / "crlf_paket";
        makePackageCopy(endPlatePkg, d, "mm_broken_pkg");
        std::string mj = readAll(d / "manifest.json");
        std::string crlf;
        for (char c : mj) {
            if (c == '\n') crlf += "\r\n";
            else crlf.push_back(c);
        }
        writeAll(d / "manifest.json", crlf);
        writeChecksums(d);  // checksums CRLF'li baytlarla hesaplanir (tutar)
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(d, reg);
        check(!r.ok, "3g CRLF'li paket REDDEDILIR");
        check(r.message.find(L"CRLF") != std::wstring::npos &&
                  r.message.find(L".gitattributes") != std::wstring::npos,
              "3g ret mesaji CRLF + .gitattributes -text onerir");
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

    // 5) ST-3g FAIL-CLOSED: okuyucunun gevsek kaldigi UC durum.
    {
        // (a) manifest.displayName YOK -> paket reddedilir (typeId'ye dusmez).
        const fs::path pA = root / "fail_displayname" / "end_plate_pkg";
        makePackageCopy(endPlatePkg, pA, "mm_fail_dn");
        {
            json mj = json::parse(readAll(pA / "manifest.json"));
            mj.erase("displayName");
            writeAll(pA / "manifest.json", mj.dump(2));
        }
        writeChecksums(pA);
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentPackageLoad r = loadComponentPackage(pA, reg);
        check(!r.ok, "5a displayName yok -> RED");
        check(r.message.find(L"displayName") != std::wstring::npos,
              "5a ret mesaji displayName'i adlandirir");
        if (!r.ok) std::printf("   5a mesaj: %ls\n", r.message.c_str());

        // (b) recipe.json argumani BOOLEAN -> RED ("1" metnine cevirmez).
        const fs::path pB = root / "fail_boolarg" / "end_plate_pkg";
        makePackageCopy(endPlatePkg, pB, "mm_fail_bool");
        {
            json rj = json::parse(readAll(pB / "recipe.json"));
            for (auto& step : rj["steps"]) {
                if (step.contains("args")) {
                    step["args"]["autoFlip"] = true;   // boolean arguman
                    break;
                }
            }
            writeAll(pB / "recipe.json", rj.dump(2));
        }
        writeChecksums(pB);
        ComponentRegistry regB;
        registerBuiltInComponents(regB);
        const ComponentPackageLoad rb = loadComponentPackage(pB, regB);
        check(!rb.ok, "5b boolean recipe argumani -> RED");
        check(rb.message.find(L"BOOLEAN") != std::wstring::npos ||
                  rb.message.find(L"boolean") != std::wstring::npos,
              "5b ret mesaji turu adlandirir");
        if (!rb.ok) std::printf("   5b mesaj: %ls\n", rb.message.c_str());

        // (c) recipeVersion 2.0.0 -> RED (yalniz 1.x okunur).
        const fs::path pC = root / "fail_recver" / "end_plate_pkg";
        makePackageCopy(endPlatePkg, pC, "mm_fail_recver");
        {
            json rj = json::parse(readAll(pC / "recipe.json"));
            rj["recipeVersion"] = "2.0.0";
            writeAll(pC / "recipe.json", rj.dump(2));
        }
        writeChecksums(pC);
        ComponentRegistry regC;
        registerBuiltInComponents(regC);
        const ComponentPackageLoad rc = loadComponentPackage(pC, regC);
        check(!rc.ok, "5c recipeVersion 2.0.0 -> RED");
        check(rc.message.find(L"recipeVersion") != std::wstring::npos,
              "5c ret mesaji recipeVersion'i adlandirir");
        if (!rc.ok) std::printf("   5c mesaj: %ls\n", rc.message.c_str());
    }

    // 6) G FAIL-CLOSED: recipe.json 'let' bolumu + yalniz CR bayti.
    {
        // (a) let bir NESNE degil -> RED.
        {
            const fs::path d = root / "fail_let_notobj" / "end_plate_pkg";
            makePackageCopy(endPlatePkg, d, "mm_fail_let_notobj");
            json rj = json::parse(readAll(d / "recipe.json"));
            rj["let"] = "metin";   // nesne degil
            writeAll(d / "recipe.json", rj.dump(2));
            writeChecksums(d);
            ComponentRegistry reg;
            registerBuiltInComponents(reg);
            const ComponentPackageLoad r = loadComponentPackage(d, reg);
            check(!r.ok, "6a let nesne degil -> RED");
            check(r.message.find(L"let") != std::wstring::npos,
                  "6a ret mesaji 'let' alanini adlandirir");
            if (!r.ok) std::printf("   6a mesaj: %ls\n", r.message.c_str());
        }
        // (b) let degeri BOOLEAN -> RED.
        {
            const fs::path d = root / "fail_let_bool" / "end_plate_pkg";
            makePackageCopy(endPlatePkg, d, "mm_fail_let_bool");
            json rj = json::parse(readAll(d / "recipe.json"));
            rj["let"] = {{"x", true}};
            writeAll(d / "recipe.json", rj.dump(2));
            writeChecksums(d);
            ComponentRegistry reg;
            registerBuiltInComponents(reg);
            const ComponentPackageLoad r = loadComponentPackage(d, reg);
            check(!r.ok, "6b let degeri boolean -> RED");
            check(r.message.find(L"x") != std::wstring::npos &&
                      r.message.find(L"boolean") != std::wstring::npos,
                  "6b ret mesaji alan adini + turu adlandirir");
            if (!r.ok) std::printf("   6b mesaj: %ls\n", r.message.c_str());
        }
        // (c) let degeri LISTE -> RED.
        {
            const fs::path d = root / "fail_let_list" / "end_plate_pkg";
            makePackageCopy(endPlatePkg, d, "mm_fail_let_list");
            json rj = json::parse(readAll(d / "recipe.json"));
            rj["let"] = {{"x", json::array({1, 2})}};
            writeAll(d / "recipe.json", rj.dump(2));
            writeChecksums(d);
            ComponentRegistry reg;
            registerBuiltInComponents(reg);
            const ComponentPackageLoad r = loadComponentPackage(d, reg);
            check(!r.ok, "6c let degeri liste -> RED");
            check(r.message.find(L"liste") != std::wstring::npos,
                  "6c ret mesaji 'liste' turunu adlandirir");
            if (!r.ok) std::printf("   6c mesaj: %ls\n", r.message.c_str());
        }
        // (d) let degeri SAYI -> RED.
        {
            const fs::path d = root / "fail_let_num" / "end_plate_pkg";
            makePackageCopy(endPlatePkg, d, "mm_fail_let_num");
            json rj = json::parse(readAll(d / "recipe.json"));
            rj["let"] = {{"x", 42}};
            writeAll(d / "recipe.json", rj.dump(2));
            writeChecksums(d);
            ComponentRegistry reg;
            registerBuiltInComponents(reg);
            const ComponentPackageLoad r = loadComponentPackage(d, reg);
            check(!r.ok, "6d let degeri sayi -> RED");
            check(r.message.find(L"sayi") != std::wstring::npos,
                  "6d ret mesaji 'sayi' turunu adlandirir");
            if (!r.ok) std::printf("   6d mesaj: %ls\n", r.message.c_str());
        }
        // (e) manifest'te YALNIZ CR bayti (CRLF degil) -> RED.
        {
            const fs::path d = root / "fail_lone_cr" / "end_plate_pkg";
            makePackageCopy(endPlatePkg, d, "mm_fail_lone_cr");
            std::string mj = readAll(d / "manifest.json");
            // Ilk satir sonuna tek basina '\r' sok (CRLF degil).
            const std::size_t nl = mj.find('\n');
            if (nl != std::string::npos) mj.insert(nl, "\r");
            writeAll(d / "manifest.json", mj);
            writeChecksums(d);   // checksums CR'li baytlarla hesaplanir (tutar)
            ComponentRegistry reg;
            registerBuiltInComponents(reg);
            const ComponentPackageLoad r = loadComponentPackage(d, reg);
            check(!r.ok, "6e yalniz CR baytli manifest -> RED");
            check(r.message.find(L"CR") != std::wstring::npos,
                  "6e ret mesaji CR baytini adlandirir");
            if (!r.ok) std::printf("   6e mesaj: %ls\n", r.message.c_str());
        }
    }

    fs::remove_all(root, ec);
    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
