// P-1/5 — PAKET DIYALOGU: sekmeler, String/Enum parametreler, documentedOnly,
//         recipe 'check' hatasinin TURKCE mesaji.
//
// Kabul olcutu (P-1 belgesi):
//   - dialog.json sekmeleri (useTabs + asTab) MM diyalog semasina TASINIR,
//   - String/Enum parametreler (malzeme, kalite) motorda GERCEKTEN kullanilir,
//   - documentedOnly parametre SEMADA isaretli kalir ve motor onu OKUMAZ
//     (degeri degistirmek geometriyi degistirmez),
//   - recipe 'check' ihlali plan.ok=false + TURKCE mesaj uretir.
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_package.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>

#ifndef MM_FIXTURES_DIR
#error "MM_FIXTURES_DIR tanimli olmali (tests/fixtures dizini)"
#endif

namespace fs = std::filesystem;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static mm::ComponentInput makeInput() {
    mm::MemberGeometry col;
    col.id = 1;
    col.axisFrom = {0, 0, 0};
    col.axisTo = {0, 0, 3000};
    col.refNormal = {1, 0, 0};
    col.secWidth = 300;
    col.secHeight = 300;
    col.sectionCenterOffset = 0.0;
    mm::ComponentInput in;
    in.main = col;
    return in;
}

int main() {
    const fs::path pkg = fs::path(MM_FIXTURES_DIR) / "p1_dialog";
    check(fs::is_directory(pkg), "0 paket fixture mevcut (p1_dialog)");

    mm::ComponentRegistry reg;
    mm::registerBuiltInComponents(reg);
    mm::ComponentDialogRegistry dialogs;
    mm::registerBuiltInDialogs(dialogs);

    const mm::ComponentPackageLoad load = mm::loadComponentPackage(pkg, reg);
    if (!load.ok) std::printf("      [teshis] yukleme hatasi: %ls\n", load.message.c_str());
    check(load.ok, "1 paket yuklendi");
    if (!load.ok) {
        std::printf("\n0/%d kontrol gecti\n", checks);
        return 1;
    }

    // --- 2) DIYALOG SEMASI: sekmeler + parametre dagitimi --------------------
    const mm::ComponentDialogSchema& sc = load.dialog.schema;
    check(sc.useTabs, "2 useTabs=true semaya tasindi");
    check(sc.groups.size() == 2, "2 iki grup (sekme) var");
    if (sc.groups.size() == 2) {
        check(sc.groups[0].title == L"Levha", "2 ilk sekme adi 'Levha'");
        check(sc.groups[1].title == L"Bulon", "2 ikinci sekme adi 'Bulon'");
        check(sc.groups[0].asTab && sc.groups[1].asTab, "2 gruplar SEKME olarak isaretli");
        check(sc.groups[0].paramNames.size() == 3, "2 'Levha' sekmesinde 3 parametre");
        check(sc.groups[1].paramNames.size() == 2, "2 'Bulon' sekmesinde 2 parametre");
        if (sc.groups[1].paramNames.size() == 2)
            check(sc.groups[1].paramNames[0] == L"kalite", "2 sekme parametre SIRASI korundu");
    }

    // --- 3) PARAMETRE TIPLERI ----------------------------------------------
    const auto findSpec = [&](const wchar_t* n) -> const mm::CompParamSpec* {
        for (const auto& s : load.type.params)
            if (s.name == n) return &s;
        return nullptr;
    };
    const mm::CompParamSpec* malzeme = findSpec(L"malzeme");
    const mm::CompParamSpec* kalite = findSpec(L"kalite");
    const mm::CompParamSpec* notlar = findSpec(L"notlar");
    check(malzeme != nullptr && malzeme->type == mm::CompParamType::String,
          "3 'malzeme' String parametresi");
    check(malzeme != nullptr && malzeme->defString == L"S355J2",
          "3 'malzeme' varsayilani S355J2");
    check(kalite != nullptr && kalite->type == mm::CompParamType::Enum,
          "3 'kalite' Enum parametresi");
    check(kalite != nullptr && kalite->choices.size() == 2,
          "3 'kalite' iki secenek sunuyor");
    check(kalite != nullptr && kalite->choices.size() == 2 && kalite->choices[1] == L"10.9",
          "3 'kalite' secenekleri korundu (10.9)");

    // --- 4) documentedOnly: semada isaretli, motor OKUMAZ -------------------
    check(notlar != nullptr, "4 'notlar' parametresi var");
    check(notlar != nullptr && notlar->documentedOnly,
          "4 'notlar' documentedOnly olarak isaretli (diyalogda gri)");
    check(malzeme != nullptr && !malzeme->documentedOnly,
          "4 documentedOnly bayragi diger parametrelere SIZMADI");

    // --- 5) MUTLU YOL: varsayilanlarla plan URETILIR ------------------------
    const mm::ComponentGenerator gen = mm::makeRecipeGenerator(load.dialog, load.recipe);
    const mm::GenerationPlan ok = gen(makeInput());
    check(ok.ok, "5 varsayilan parametrelerle plan uretildi");
    check(ok.plates.size() == 1, "5 tek levha uretildi");
    if (ok.ok && ok.plates.size() == 1) {
        check(ok.plates[0].material == L"S355J2",
              "5 String parametre (malzeme) levhaya YAZILDI");
        check(ok.plates[0].name == L"8.8", "5 Enum parametre (kalite) levhaya YAZILDI");
        check(ok.plates[0].thickness == 20.0, "5 Length parametre (bp_t) 20 mm");
    }

    // --- 6) documentedOnly DEGISTIRMEK GEOMETRIYI DEGISTIRMEZ ---------------
    {
        mm::ComponentInput in = makeInput();
        in.params.push_back({L"notlar", mm::CompParamValue{std::wstring(L"bu not motoru ilgilendirmez")}});
        const mm::GenerationPlan p = gen(in);
        check(p.ok, "6 documentedOnly degeri plani BOZMADI");
        check(p.ok && p.plates.size() == 1 && p.plates[0].thickness == 20.0,
              "6 documentedOnly geometriyi DEGISTIRMEDI");
    }

    // --- 7) CHECK IHLALI: ok=false + TURKCE mesaj --------------------------
    {
        mm::ComponentInput in = makeInput();
        in.params.push_back({L"bp_t", mm::CompParamValue{10.0}});  // 12'den kucuk
        const mm::GenerationPlan bad = gen(in);
        check(!bad.ok, "7 check ihlali plani REDDETTI (ok=false)");
        const bool trMsg = bad.error.find(L"en az 12 mm") != std::wstring::npos;
        std::printf("      [teshis] check mesaji: %ls\n", bad.error.c_str());
        check(trMsg, "7 Turkce check mesaji GenerationPlan.error'da");
        check(!bad.checks.empty() && !bad.checks[0].pass, "7 ihlal eden check RAPORLANDI");
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
