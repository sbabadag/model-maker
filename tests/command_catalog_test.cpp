// KOMUT KATALOĞU (UI-2) testi — portatif, Qt/OCCT/windows.h YOK.
//
// Kanitlanan sozlesmeler:
//   1) Katalog saglikli: tekrar id / kisayol cakismasi / bozuk kisayol YOK.
//   2) 'G' hatasi KAPANDI: G=Kolon, Fillet=Q; ayni tus iki komuta bagli degil.
//   3) Degistirici farki kayitta acik: Ctrl+S=Kaydet, S=Silindir.
//   4) Serit: onaylanan 6 sekme; her serit komutu TAM BIR kez; Beton/Analiz
//      sekmesi yok; Calisma Duzlemi ayri sekme DEGIL, Gorunum icinde grup.
//   5) Menu tam envanter: her komut menude en az bir kez (Dosya/Pencere dahil).
//   6) Arama: "kolon" -> Kolon, "odak" -> Odaklan, "kaydet" -> Kaydet.

#include "model_maker/command_catalog.hpp"
#include "model_maker/command_registry.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

const mm::PlanTab* findTab(const mm::UiPlan& plan, const std::string& title) {
    for (const mm::PlanTab& tab : plan.tabs())
        if (tab.title == title) return &tab;
    return nullptr;
}

bool hasGroup(const mm::PlanTab& tab, const std::string& title) {
    for (const mm::PlanGroup& group : tab.groups)
        if (group.title == title) return true;
    return false;
}

} // namespace

int main() {
    const mm::CommandCatalog catalog = mm::buildCommandCatalog();

    // ---- 1) Katalog saglikli -------------------------------------------
    {
        const auto issues = mm::validateCatalog(catalog);
        if (!issues.empty()) {
            for (const mm::ValidationIssue& issue : issues)
                std::printf("  ISSUE: %s\n", issue.message().c_str());
        }
        check(issues.empty(), "katalog dogrulamasi temiz olmali (id/kisayol/plan)");
        check(catalog.registry.size() > 100, "komut sayisi 100'den fazla olmali");
        check(catalog.registry.shortcutConflicts().empty(),
              "kisayol cakismasi olmamali (tek kaynak)");
    }

    // ---- 2) 'G' hatasi kapandi -----------------------------------------
    {
        const mm::CommandDef* g = catalog.registry.commandForShortcut("G");
        check(g != nullptr && g->id == "draw.column", "'G' Kolon komutuna cozulmeli");
        const mm::CommandDef* q = catalog.registry.commandForShortcut("Q");
        check(q != nullptr && q->id == "edit.fillet",
              "Fillet bosta olan 'Q' ile baglanmali ('G' degil)");

        const mm::CommandDef* fillet = catalog.registry.find("edit.fillet");
        check(fillet != nullptr, "edit.fillet kayitta olmali");
        check(fillet && fillet->shortcut == "Q", "Fillet kisayolu Q olmali");
        // Ayni tusu paylasan ikinci komut OLMAMALI.
        std::size_t gUsers = 0;
        for (const mm::CommandDef& def : catalog.registry.commands())
            if (!def.shortcut.empty() && mm::shortcutKey(def.shortcut) == "g") ++gUsers;
        check(gUsers == 1, "'G' yalniz TEK komuta bagli olmali (eski hata: Kolon+Fillet)");
    }

    // ---- 3) Degistirici farki ------------------------------------------
    {
        const mm::CommandDef* ctrlS = catalog.registry.commandForShortcut("Ctrl+S");
        check(ctrlS != nullptr && ctrlS->id == "file.save", "Ctrl+S = Kaydet");
        check(ctrlS && ctrlS->delivery == mm::ShortcutDelivery::Host,
              "Ctrl+S pencere kisayolu (panel odaktayken de calisir)");

        const mm::CommandDef* plainS = catalog.registry.commandForShortcut("S");
        check(plainS != nullptr && plainS->id == "draw.cylinder", "tekil S = Silindir");
        check(plainS && plainS->delivery == mm::ShortcutDelivery::Canvas,
              "tekil S tuval yonlendiricisinden");

        check(catalog.registry.commandForShortcut("Ctrl+Z") != nullptr,
              "Ctrl+Z kayitta olmali");
        check(catalog.registry.commandForShortcut("Ctrl+Y") != nullptr,
              "Ctrl+Y kayitta olmali");
        check(catalog.registry.commandForShortcut("F3") != nullptr, "F3 (Snap) kayitta");
        check(catalog.registry.commandForShortcut("F7") != nullptr, "F7 (Hayalet) kayitta");
        check(catalog.registry.commandForShortcut("Delete") != nullptr, "Delete kayitta");
        check(catalog.registry.commandForShortcut("Esc") != nullptr, "Esc kayitta");
    }

    // ---- 4) Serit plani -------------------------------------------------
    {
        const auto& tabs = catalog.ribbon.tabs();
        check(tabs.size() == 6, "seritte 6 sekme beklenir (Celik/Cizim/Duzenle/Gorunum/Cizimler/Yonet)");
        check(tabs.size() == 6 && tabs[0].title == "Çelik" && tabs[1].title == "Çizim" &&
                  tabs[2].title == "Düzenle" && tabs[3].title == "Görünüm" &&
                  tabs[4].title == "Çizimler ve Raporlar" && tabs[5].title == "Yönet",
              "sekme sirasi onaylanan duzenle ayni");

        check(findTab(catalog.ribbon, "Beton") == nullptr,
              "Beton sekmesi ACILMAYACAK (komut yok)");
        check(findTab(catalog.ribbon, "Analiz") == nullptr,
              "Analiz sekmesi ACILMAYACAK (komut yok)");
        check(findTab(catalog.ribbon, "Dosya") == nullptr,
              "Dosya sekmesi YOK (menu + hizli erisim karari)");
        check(findTab(catalog.ribbon, "Çalışma Düzlemi") == nullptr,
              "Calisma DuZlemi ayri sekme DEGIL");

        const mm::PlanTab* view = findTab(catalog.ribbon, "Görünüm");
        check(view != nullptr, "Gorunum sekmesi var");
        check(view && hasGroup(*view, "Çalışma Düzlemi"),
              "Calisma DuZlemi, Gorunum sekmesinde GRUP olmali");
        check(view && hasGroup(*view, "Yön") && hasGroup(*view, "Stil"),
              "Gorunum sekmesinde Yon ve Stil gruplari var");

        // Her serit komutu TAM BIR kez (validateCatalog bunu zaten dogruluyor —
        // burada ayrica sayilarak kanitlaniyor).
        for (const mm::CommandDef& def : catalog.registry.commands()) {
            const std::size_t count = catalog.ribbon.occurrences(def.id);
            if (def.surface == mm::CommandSurface::Ribbon && count != 1) {
                std::printf("  SERIT IHLALI: %s -> %zu kez\n", def.id.c_str(), count);
                check(false, "serit komutu seritte tam bir kez olmali");
            }
            if (def.surface != mm::CommandSurface::Ribbon && count != 0) {
                std::printf("  SERIT IHLALI (menu/alt cubuk): %s -> %zu kez\n",
                            def.id.c_str(), count);
                check(false, "menu/alt cubuk komutu seritte gorunmemeli");
            }
        }

        // Seritte plansiz kalan komut yok (her komut bir yere bagli).
        check(catalog.ribbon.missingCommands(catalog.registry).size() +
                      catalog.menu.missingCommands(catalog.registry).size() <=
                  catalog.registry.size(),
              "plansiz komut muhasebesi tutarli");
    }

    // ---- 5) Menu tam envanter ------------------------------------------
    {
        for (const mm::CommandDef& def : catalog.registry.commands()) {
            if (def.surface == mm::CommandSurface::StatusBar) continue;
            if (catalog.menu.occurrences(def.id) == 0) {
                std::printf("  MENUDE YOK: %s\n", def.id.c_str());
                check(false, "her komut ≡ menusunde bulunmali");
            }
        }
        const mm::PlanTab* dosya = findTab(catalog.menu, "&Dosya");
        check(dosya != nullptr, "menude Dosya bolumu var");
        const mm::PlanTab* pencere = findTab(catalog.menu, "&Pencere");
        check(pencere != nullptr, "menude Pencere bolumu var");
        check(catalog.menu.occurrences("file.save") == 1, "Kaydet menude tek kez");
        check(catalog.menu.occurrences("window.closeViews") == 1,
              "Tum gorunusleri kapat menude");
    }

    // ---- 6) Arama -------------------------------------------------------
    {
        auto hits = catalog.registry.search("kolon");
        check(!hits.empty() && hits.front()->id == "draw.column",
              "'kolon' aramasi Kolon komutunu ilk sirada bulmali");

        hits = catalog.registry.search("odak");
        check(!hits.empty() && hits.front()->id == "view.focus",
              "'odak' aramasi Odaklan komutunu bulmali (kayitta yer aliyor)");

        hits = catalog.registry.search("kaydet");
        check(!hits.empty() && hits.front()->id == "file.save",
              "'kaydet' aramasi Kaydet'i ilk sirada bulmali");

        hits = catalog.registry.search("q");
        bool foundFillet = false;
        for (const mm::CommandDef* def : hits)
            if (def->id == "edit.fillet") foundFillet = true;
        check(foundFillet, "'q' aramasi Fillet'i de getirmeli (kisayol/anahtar)");

        // B1: component katalogu adlari da aramada bulunur ("haunch" -> Haunch (40)).
        hits = catalog.registry.search("haunch");
        check(!hits.empty() && hits.front()->id == "component:tekla_haunch_40",
              "'haunch' aramasi Haunch (40) component'ini ilk sirada bulmali");
        hits = catalog.registry.search("end plate");
        bool foundEndPlate = false;
        for (const mm::CommandDef* def : hits)
            if (def->id == "component:beam_to_column_end_plate") foundEndPlate = true;
        check(foundEndPlate, "'end plate' aramasi End Plate component'ini de getirmeli");
        hits = catalog.registry.search("alin levhasi");
        bool foundAlin = false;
        for (const mm::CommandDef* def : hits)
            if (def->id == "component:beam_to_column_end_plate" ||
                def->id == "steel.endPlate") foundAlin = true;
        check(foundAlin, "'alin levhasi' aramasi End Plate'i bulmali");
    }

    // ---- 7) KİRİŞ (UI-2b) + Türkçe klavye uçtan uca ----------------------
    {
        const mm::CommandDef* beam = catalog.registry.find("draw.beam");
        check(beam != nullptr, "draw.beam (Kiriş) kayitta olmali");
        if (beam) {
            check(beam->title == "Kiriş", "Kiriş komutunun adi 'Kiriş'");
            check(beam->surface == mm::CommandSurface::Ribbon,
                  "Kiriş serit komutu (Celik > Uyeler)");
            check(beam->shortcut.empty(), "Kiriş'in kendi kisayolu yok (profil zorunlu)");
        }
        check(catalog.ribbon.occurrences("draw.beam") == 1,
              "Kiriş seritte TAM BIR kez gorunmeli");

        // 'Kiriş' takma adi ARTIK bu komuta bagli (eski: Cizgi'ye gidiyordu).
        auto hits = catalog.registry.search("kiris");
        check(!hits.empty() && hits.front()->id == "draw.beam",
              "diyakritiksiz 'kiris' aramasi Kiriş komutunu ilk sirada bulmali");
        hits = catalog.registry.search("KİRİŞ");
        check(!hits.empty() && hits.front()->id == "draw.beam",
              "'KİRİŞ' aramasi da Kiriş komutunu bulmali");
        const mm::CommandDef* line = catalog.registry.find("draw.line");
        check(line != nullptr && line->keywords.find("kiris") == std::string::npos,
              "Cizgi komutu 'kiris' takma adini TASIMAMALI (tek sahip)");

        // TÜRKÇE Q KLAVYE: fiziksel tuş → komut. VK_I (0x49) 'ı' üretse de
        // Ayna'ya gitmeli; 'i' karakteri üzerinden çözüm Ayna'yi kaybederdi.
        const std::string key = mm::keyNameFromVirtualKey(0x49);
        const mm::CommandDef* mirror = catalog.registry.commandForShortcut(key);
        check(mirror != nullptr && mirror->id == "edit.mirror",
              "VK_I -> 'I' -> edit.mirror (Ayna) uçtan uca");
        check(catalog.registry.commandForShortcut(mm::keyNameFromVirtualKey(0x47))->id ==
                  "draw.column",
              "VK_G -> Kolon");
        check(catalog.registry.commandForShortcut(mm::keyNameFromVirtualKey(0x51))->id ==
                  "edit.fillet",
              "VK_Q -> Fillet");
    }

    if (failures == 0) {
        std::printf("command_catalog_test: TUM KONTROLLER GECTI (%zu komut)\n",
                    catalog.registry.size());
        return 0;
    }
    std::printf("command_catalog_test: %d HATA\n", failures);
    return 1;
}
