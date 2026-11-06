// KOMUT KAYDI (UI-2) — portatif cekirdek testi. Qt/OCCT/windows.h YOK.
//
// Kapsam:
//   1) Kisayol ayristirma/normalize: "Ctrl+S" ile "S" AYRI bilesimler,
//      gecersiz metin sessizce yutulmaz.
//   2) 'G' CAKISMASI: bugun Kolon ve Fillet ayni tusu paylasiyor ve Fillet
//      OLU kaliyor. Kayit bu hatayi validate() ile YAKALAMALI — burada
//      kasitli olarak cakisan bir kayit kurulup hata kanitlaniyor.
//   3) Arama: Turkce diyakritiksiz yazim ("kiris"), buyuk/kucuk harf,
//      anahtar sozcuk (takma ad) ve bos sorgu.
//   4) Plan: bilinmeyen id, seritte tekrar, bos grup, plansiz komut.

#include "model_maker/command_registry.hpp"

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

void checkEq(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        ++failures;
        std::printf("FAIL: %s (beklenen '%s', gelen '%s')\n", message,
                    expected.c_str(), actual.c_str());
    }
}

mm::CommandDef cmd(std::string id, std::string title, std::string shortcut = {},
                   mm::ShortcutDelivery delivery = mm::ShortcutDelivery::None) {
    mm::CommandDef def;
    def.id = std::move(id);
    def.title = std::move(title);
    def.shortcut = std::move(shortcut);
    def.delivery = delivery;
    return def;
}

bool hasIssue(const std::vector<mm::ValidationIssue>& issues, mm::ValidationIssue::Code code) {
    for (const mm::ValidationIssue& issue : issues)
        if (issue.code == code) return true;
    return false;
}

bool hasIssueFor(const std::vector<mm::ValidationIssue>& issues, mm::ValidationIssue::Code code,
                 const std::string& id) {
    for (const mm::ValidationIssue& issue : issues)
        if (issue.code == code && issue.id == id) return true;
    return false;
}

} // namespace

int main() {
    // ---- 1) Kisayol ayristirma -------------------------------------------
    {
        const auto ctrlS = mm::parseShortcut("Ctrl+S");
        check(ctrlS.has_value(), "Ctrl+S ayristirilmali");
        check(ctrlS && ctrlS->ctrl && !ctrlS->alt && !ctrlS->shift && ctrlS->key == "S",
              "Ctrl+S: ctrl=1, tus=S");

        const auto plainS = mm::parseShortcut("s");
        check(plainS.has_value() && !plainS->ctrl && plainS->key == "S",
              "tekil s: degistirici yok, tus=S (buyuk harfe normalize)");

        // AYNI TUSUN DEGISTIRICI FARKI KAYITTA ACIK OLMALI (kullanici sarti).
        check(mm::shortcutKey("Ctrl+S") == "ctrl+s", "Ctrl+S kanonik anahtar");
        check(mm::shortcutKey("S") == "s", "tekil S kanonik anahtar");
        check(mm::shortcutKey("Ctrl+S") != mm::shortcutKey("S"),
              "Ctrl+S ile S AYNI kisayol degildir");
        check(mm::shortcutKey("ctrl+s") == mm::shortcutKey("Control+S"),
              "Ctrl/Control ve harf buyuklugu ayni bilesim");

        const auto shiftV = mm::parseShortcut("Ctrl+Shift+V");
        check(shiftV && shiftV->ctrl && shiftV->shift && !shiftV->alt && shiftV->key == "V",
              "Ctrl+Shift+V cozulur");
        checkEq(mm::formatShortcut(*shiftV), "Ctrl+Shift+V", "kanonik gosterim sirasi");

        const auto alt1 = mm::parseShortcut("Alt+1");
        check(alt1 && alt1->alt && !alt1->ctrl && alt1->key == "1", "Alt+1 cozulur");

        const auto f7 = mm::parseShortcut("F7");
        check(f7 && f7->key == "F7" && !f7->ctrl, "F7 cozulur");

        const auto del = mm::parseShortcut("Delete");
        check(del && del->key == "DELETE", "Delete -> DELETE");
        checkEq(mm::formatShortcut(*del), "Delete", "DELETE gosterimi 'Delete'");

        check(!mm::parseShortcut("").has_value(), "bos metin gecersiz");
        check(!mm::parseShortcut("Ctrl").has_value(), "yalniz degistirici gecersiz");
        check(!mm::parseShortcut("Ctrl++").has_value(), "bos parca gecersiz");
        check(!mm::parseShortcut("S+Ctrl").has_value(), "tus sonda olmali");
        check(!mm::parseShortcut("Hyper+S").has_value(), "bilinmeyen degistirici gecersiz");
        check(mm::shortcutKey("bozuk").empty(), "gecersiz kisayolun anahtari bos");
    }

    // ---- 2) 'G' CAKISMASI — kayit hatayi YAKALAMALI -----------------------
    {
        mm::CommandRegistry broken;
        broken.add(cmd("steel.column", "Kolon", "G", mm::ShortcutDelivery::Canvas));
        broken.add(cmd("edit.fillet", "Fillet", "G", mm::ShortcutDelivery::Canvas));

        const auto issues = broken.validate();
        check(hasIssue(issues, mm::ValidationIssue::Code::DuplicateShortcut),
              "'G' iki komuta baglanirsa DuplicateShortcut raporlanmali");
        check(hasIssueFor(issues, mm::ValidationIssue::Code::DuplicateShortcut, "edit.fillet"),
              "cakisma ikinci komutun id'siyle raporlanmali");

        const auto conflicts = broken.shortcutConflicts();
        check(conflicts.size() == 1,
              "tek cakisma cifti bekleniyor (G: Kolon + Fillet)");
        if (conflicts.size() == 1) {
            check(conflicts[0].first == "steel.column" && conflicts[0].second == "edit.fillet",
                  "cakisma cifti (ilk tanim, ikinci tanim) sirasinda");
        }

        // Kayit cozerken ILK tanimi dondurur => eski kodda Fillet OLU kaliyordu.
        const mm::CommandDef* winner = broken.commandForShortcut("g");
        check(winner && winner->id == "steel.column",
              "cakismada ilk tanim kazanir (eski hatanin nedeni)");

        // DUZELTME: Fillet'e bosta bir tus (Q) verilince kayit temiz olmali.
        mm::CommandRegistry fixed;
        fixed.add(cmd("steel.column", "Kolon", "G", mm::ShortcutDelivery::Canvas));
        fixed.add(cmd("edit.fillet", "Fillet", "Q", mm::ShortcutDelivery::Canvas));
        check(fixed.validate().empty(), "G + Q ile cakisma kalmamali");
        const mm::CommandDef* fillet = fixed.commandForShortcut("q");
        check(fillet && fillet->id == "edit.fillet", "Q Fillet'e cozulur");
    }

    // ---- 3) Kayit dogrulamalari ------------------------------------------
    {
        mm::CommandRegistry registry;
        registry.add(cmd("a.one", "Bir", "Ctrl+S", mm::ShortcutDelivery::Host));
        registry.add(cmd("a.one", "Bir daha", "", mm::ShortcutDelivery::None));
        check(hasIssue(registry.validate(), mm::ValidationIssue::Code::DuplicateId),
              "ayni id iki kez tanimlanirsa DuplicateId");

        mm::CommandRegistry invalid;
        invalid.add(cmd("b.bad", "Bozuk", "Ctrl+-+X", mm::ShortcutDelivery::Host));
        check(hasIssue(invalid.validate(), mm::ValidationIssue::Code::InvalidShortcut),
              "ayristirilamayan kisayol InvalidShortcut");

        mm::CommandRegistry orphan;
        orphan.add(cmd("c.key", "Tussuz", "F9", mm::ShortcutDelivery::None));
        check(hasIssue(orphan.validate(), mm::ValidationIssue::Code::MissingShortcutId),
              "kisayol metni var ama teslim yolu yok -> MissingShortcutId");

        mm::CommandRegistry emptyTitle;
        emptyTitle.add(cmd("d.empty", "", "", mm::ShortcutDelivery::None));
        check(hasIssue(emptyTitle.validate(), mm::ValidationIssue::Code::EmptyTitle),
              "bos gorunen ad raporlanmali");

        mm::CommandRegistry toggle;
        mm::CommandDef snap = cmd("e.snap", "Snap", "F3", mm::ShortcutDelivery::Canvas);
        snap.kind = mm::CommandKind::Toggle;
        toggle.add(snap);
        check(hasIssue(toggle.validate(), mm::ValidationIssue::Code::EmptyTitle),
              "ac-kapa komutunun durum anahtari bos olamaz");
        snap.toggleKey = "snap";
        mm::CommandRegistry toggleOk;
        toggleOk.add(snap);
        check(toggleOk.validate().empty(), "durum anahtari doluysa kayit saglikli");
    }

    // ---- 4) Arama ---------------------------------------------------------
    {
        mm::CommandRegistry registry;
        mm::CommandDef kiris = cmd("steel.beam", "Kiriş", "", mm::ShortcutDelivery::None);
        kiris.keywords = "beam kiris kiris beam";
        kiris.tooltip = "İki nokta arası üye";
        registry.add(kiris);
        registry.add(cmd("file.save", "Kaydet", "Ctrl+S", mm::ShortcutDelivery::Host));
        mm::CommandDef focus = cmd("view.focus", "Odaklan", "F", mm::ShortcutDelivery::Canvas);
        focus.keywords = "odak zoom secim";
        registry.add(focus);
        registry.add(cmd("steel.column", "Kolon", "G", mm::ShortcutDelivery::Canvas));

        auto hits = registry.search("kiris");
        check(!hits.empty() && hits.front()->id == "steel.beam",
              "diyakritiksiz 'kiris' aramasi Kiriş komutunu bulmali");

        hits = registry.search("KİRİŞ");
        check(!hits.empty() && hits.front()->id == "steel.beam",
              "buyuk harf/diyakritikli arama da bulmali");

        hits = registry.search("KAYDET");
        check(!hits.empty() && hits.front()->id == "file.save",
              "arama buyuk/kucuk harf duyarsiz");

        hits = registry.search("odak");
        check(!hits.empty() && hits.front()->id == "view.focus",
              "anahtar sozcuk (takma ad) ile arama");

        hits = registry.search("kay");
        check(!hits.empty() && hits.front()->id == "file.save",
              "onek aramasi Kaydet'i ilk sirada getirmeli");

        hits = registry.search("yok boyle komut");
        check(hits.empty(), "eslesmeyen sorgu bos donmeli");

        hits = registry.search("");
        check(hits.size() <= 12, "bos sorgu limit ile sinirli");
        check(!hits.empty(), "bos sorgu tum komutlari listeler");
    }

    // ---- 5) Plan dogrulamalari -------------------------------------------
    {
        mm::CommandRegistry registry;
        registry.add(cmd("one", "Bir", "", mm::ShortcutDelivery::None));
        registry.add(cmd("two", "İki", "", mm::ShortcutDelivery::None));

        mm::UiPlan plan;
        mm::PlanTab tab;
        tab.title = "Sekme";
        mm::PlanGroup group;
        group.title = "Grup";
        mm::PlanItem item;
        item.kind = mm::PlanItem::Kind::Command;
        item.id = "one";
        group.items.push_back(item);
        item.id = "two";
        group.items.push_back(item);
        item.id = "one";  // seritte tekrar
        group.items.push_back(item);
        tab.groups.push_back(group);
        plan.addTab(tab);

        const auto issues = plan.validate(registry);
        check(hasIssue(issues, mm::ValidationIssue::Code::DuplicateInPlan),
              "seritte ayni komut iki kez -> DuplicateInPlan");
        check(plan.occurrences("one") == 2, "occurrences tekrarlari sayar");

        mm::UiPlan unknown;
        mm::PlanTab bad;
        bad.title = "Sekme";
        mm::PlanGroup badGroup;
        badGroup.title = "Grup";
        mm::PlanItem ghostItem;
        ghostItem.id = "hayalet";
        badGroup.items.push_back(ghostItem);
        bad.groups.push_back(badGroup);
        unknown.addTab(bad);
        check(hasIssue(unknown.validate(registry), mm::ValidationIssue::Code::UnknownId),
              "planda kayitta olmayan id -> UnknownId");

        mm::UiPlan emptyPlan;
        mm::PlanTab emptyTab;
        emptyTab.title = "Bos";
        emptyPlan.addTab(emptyTab);
        check(hasIssue(emptyPlan.validate(registry), mm::ValidationIssue::Code::EmptyPlan),
              "bos sekme -> EmptyPlan");

        // Menu ici komutlar da plan sayilir (seritte olmayan komut raporlanir).
        mm::UiPlan menuOnly;
        mm::PlanTab menuTab;
        menuTab.title = "Menu";
        mm::PlanGroup menuGroup;
        menuGroup.title = "Alt";
        mm::PlanItem subMenu;
        subMenu.kind = mm::PlanItem::Kind::Menu;
        subMenu.id = "alt.menu";
        mm::PlanItem nested;
        nested.id = "two";
        subMenu.children.push_back(nested);
        menuGroup.items.push_back(subMenu);
        menuTab.groups.push_back(menuGroup);
        menuOnly.addTab(menuTab);
        const auto missing = menuOnly.missingCommands(registry);
        check(missing.size() == 1 && missing[0] == "one",
              "menu ici komut planlanmis sayilir (yalniz 'one' eksik)");
        check(menuOnly.occurrences("two") == 1, "menu ici komut occurrences'a girer");
    }

    if (failures == 0) {
        std::printf("command_registry_test: TUM KONTROLLER GECTI\n");
        return 0;
    }
    std::printf("command_registry_test: %d HATA\n", failures);
    return 1;
}
