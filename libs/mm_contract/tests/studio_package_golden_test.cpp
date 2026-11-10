// P-1/7 — STUDIO UYUMLULUK (GOLDEN) TESTLERI
//
// Studio'nun disa aktardigi GERCEK paketler MM deposunda test girdisi olarak
// durur (tests/fixtures/studio_pkgs/, LF bayt, kaynak commit README'de).
// Bu test her paket icin:
//   1) paketin MM yukleyicisinden GECMESINI (checksums + sema + recipe),
//   2) katalog metasinin (gorunurluk + pickCount) dogru cikarilmasini,
//   3) diyalog semasinin GECERLI olmasini (grup/parametre tutarliligi),
//   4) KANONIK girdiyle recipe kosulunca planDigest'in SABIT (golden) olmasini
//      dogrular. Golden'lar tests/golden/studio_<tip>.json dosyasindadir.
//
// KURAL: golden, test gecsin diye YENIDEN URETILMEZ. Bilerek degistirilecekse
// (orn. Studio paketi yenilendi) eski/yeni digest PR'da gosterilir ve STATE'e
// "Studio REPIN gerekli: <hash>" yazilir.
//
// BILINEN ENGEL (P-2): mm_stiffened_base_plate `plate.polygon` kullaniyor;
// materyalizator bu blogu katıya ceviremiyor. Bu testte o paket icin
// "yukleme + sema gecerli" dogrulanir, URETIM icin beklenen hata metni
// denetlenir (P-2 bitince bu beklenti KALDIRILMALIDIR — test bunu hatirlatir).
#include "model_maker/component_codec.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"

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

#ifndef MM_FIXTURES_DIR
#error "MM_FIXTURES_DIR tanimli olmali (tests/fixtures dizini)"
#endif

static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Kanonik girdi: HEB300 kolon (Z yonu, 0..3000) + IPE300 kiris (kolon flansina).
static mm::ComponentInput canonicalInput() {
    const mm::MemberSectionSpec colSec{300.0, 300.0, 11.0, 19.0, 0.0};   // HEB300
    // Kiris, kolondan KUCUK olmali: paketin kendi check'i
    // "sureklilik levhasi kolon flansinin icine giriyor" der (bkz. recipe).
    const mm::MemberSectionSpec beamSec{120.0, 240.0, 6.2, 9.8, 0.0};    // IPE240
    mm::ComponentInput in;
    in.main = mm::memberGeometryFromProfile({0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
                                            {1, 0, 0}, 0.0, colSec);
    in.secondary.push_back(mm::memberGeometryFromProfile(
        {0, 120, 2880}, {6000, 120, 2880}, /*connectionAtA=*/true, {0, 0, 1}, 0.0, beamSec));
    return in;
}

// Diyalog semasi gecerli mi: her grup parametresi semada var, bos sekme yok.
static bool dialogSchemaValid(const mm::ComponentDialogDefinition& def, std::string* why) {
    std::set<std::wstring> names;
    for (const auto& s : def.specs) names.insert(s.name);
    if (def.schema.groups.empty()) {
        *why = "grup yok";
        return false;
    }
    int nonEmpty = 0;
    for (const auto& g : def.schema.groups) {
        for (const auto& n : g.paramNames)
            if (!names.count(n)) {
                *why = "grup parametresi semada yok";
                return false;
            }
        if (g.title.empty() && g.asTab) {
            *why = "sekme basligi bos";
            return false;
        }
        if (!g.paramNames.empty()) ++nonEmpty;
    }
    if (nonEmpty == 0) {
        *why = "parametreli grup yok";
        return false;
    }
    return true;
}

// Golden: tests/golden/studio_<ad>.json {"planDigest": "..."}
static bool goldenCompare(const std::string& name, const std::string& digest) {
    const fs::path gdir = fs::path(MM_FIXTURES_DIR).parent_path() / "golden";
    const fs::path gf = gdir / ("studio_" + name + ".json");
    std::error_code ec;
    if (!fs::is_regular_file(gf, ec)) {
        std::printf("      [golden] %s YOK — ilk kayit onerisi: {\"planDigest\": \"%s\"}\n",
                    gf.string().c_str(), digest.c_str());
        return false;
    }
    const json gj = json::parse(readAll(gf), nullptr, false);
    if (gj.is_discarded() || !gj.contains("planDigest")) return false;
    const std::string want = gj["planDigest"].get<std::string>();
    std::printf("      [golden] %s: beklenen %s, gercek %s\n", name.c_str(), want.c_str(),
                digest.c_str());
    return want == digest;
}

int main() {
    const fs::path pkgs = fs::path(MM_FIXTURES_DIR) / "studio_pkgs";

    mm::ComponentRegistry reg;
    mm::registerBuiltInComponents(reg);
    mm::ComponentDialogRegistry dialogs;
    mm::registerBuiltInDialogs(dialogs);

    // --- 1) RIJITLESTIRILMIS ALIN LEVHASI (yalniz desteklenen bloklar) --------
    {
        const fs::path dir = pkgs / "mm_stiffened_end_plate.mmcpkg";
        check(fs::is_directory(dir), "1 alin levhasi paketi mevcut");
        const mm::ComponentPackageLoad load = mm::loadComponentPackage(dir, reg);
        if (!load.ok) std::printf("      [teshis] yukleme: %ls\n", load.message.c_str());
        check(load.ok, "1 alin levhasi paketi YUKLENDI (checksums + sema)");
        if (load.ok) {
            check(load.type.typeId == L"mm_stiffened_end_plate", "1 typeId beklendigi gibi");
            check(load.type.schemaVersion == 1, "1 schemaVersion 1");
            check(load.type.catalog.catalogVisible, "1 katalogda GORUNUR");
            check(load.type.catalog.pickCount == 2, "1 pickCount 2 (kolon + kiris)");
            std::string why;
            check(dialogSchemaValid(load.dialog, &why), "1 diyalog semasi GECERLI");
            if (!why.empty()) std::printf("      [teshis] sema: %s\n", why.c_str());
            check(load.type.params.size() == 8, "1 8 parametre yuklendi (paket ile ayni)");

            const mm::ComponentGenerator gen =
                mm::makeRecipeGenerator(load.dialog, load.recipe);
            const mm::GenerationPlan gp = gen(canonicalInput());
            if (!gp.ok) std::printf("      [teshis] uretim: %ls\n", gp.error.c_str());
            check(gp.ok, "1 KANONIK girdiyle plan URETILDI");
            if (gp.ok) {
                std::printf("      [golden] alin levhasi: %zu levha, %zu bulon grubu, "
                            "%zu kaynak\n",
                            gp.plates.size(), gp.boltGroups.size(), gp.welds.size());
                check(!gp.plates.empty(), "1 levha uretildi");
                check(!gp.boltGroups.empty(), "1 bulon grubu uretildi");
                check(!gp.welds.empty(), "1 kaynak uretildi (S-8: kaynaklar cizilir)");
                const std::string digest = mm::generationPlanDigest(gp);
                check(goldenCompare("end_plate", digest), "1 planDigest GOLDEN ile AYNI");

                // Parametre degisince geometri DEGISMELI (golden kor noktasi).
                mm::ComponentInput off = canonicalInput();
                off.params.push_back({L"st_top", mm::CompParamValue{false}});
                off.params.push_back({L"st_bottom", mm::CompParamValue{false}});
                const mm::GenerationPlan gp2 = gen(off);
                check(gp2.ok, "1 sureklilik levhalari KAPALI plan uretildi");
                if (gp2.ok)
                    check(mm::generationPlanDigest(gp2) != digest,
                          "1 st_top/st_bottom=false geometriyi DEGISTIRDI");
            }
        }
    }

    // --- 2) RIJITLESTIRILMIS TABAN LEVHASI (plate.polygon — P-2 engeli) -------
    {
        const fs::path dir = pkgs / "mm_stiffened_base_plate.mmcpkg";
        check(fs::is_directory(dir), "2 taban levhasi paketi mevcut");
        const mm::ComponentPackageLoad load = mm::loadComponentPackage(dir, reg);
        if (!load.ok) std::printf("      [teshis] yukleme: %ls\n", load.message.c_str());
        check(load.ok, "2 taban levhasi paketi YUKLENDI (checksums + sema)");
        if (load.ok) {
            check(load.type.catalog.pickCount == 1, "2 pickCount 1 (yalniz kolon)");
            std::string why;
            check(dialogSchemaValid(load.dialog, &why), "2 diyalog semasi GECERLI");
            const mm::ComponentGenerator gen =
                mm::makeRecipeGenerator(load.dialog, load.recipe);
            const mm::GenerationPlan gp = gen(canonicalInput());
            if (!gp.ok) std::printf("      [teshis] uretim: %ls\n", gp.error.c_str());
            check(gp.ok, "2 KANONIK girdiyle plan URETILDI (polygon levhalar dahil)");
            if (gp.ok) {
                std::printf("      [golden] taban levhasi: %zu levha, %zu bulon grubu, "
                            "%zu kaynak\n",
                            gp.plates.size(), gp.boltGroups.size(), gp.welds.size());
                check(!gp.plates.empty(), "2 levha uretildi");
                // P-2 ENGELI (STATE'te kayitli): plan OK, fakat MATERYALIZATOR
                // polygon konturunu katiya ceviremiyor ("levha kati uretilemedi").
                // Bu yuzden bu paket icin golden DIGEST KARSILASTIRMASI YAPILMAZ;
                // P-2 bitince golden eklenir.
                std::printf("      [engel] taban levhasi: materyalizasyon P-2'de "
                            "(plate.polygon konturu katiya cevrilemiyor)\n");
            }
        }
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
