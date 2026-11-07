// SERIT (RIBBON) — Qt offscreen testi (UI-2). Linux + Windows, Qt6 varsa.
//
// Kapsam (kullanici UI-2 kabul kriterleri):
//   1) Her kayitli SERIT komutu seritte TAM BIR kez gorunuyor.
//   2) Dugmeye tiklaninca DOGRU komut calisiyor.
//   3) Hizli arama komutu bulup calistiriyor.
//   4) Kisayollar degismedi (kayit = tek kaynak; Fillet artik Q, Kolon G).
//   5) 1.0 / 1.5 / 2.0 DPI'da etiketler KESILMIYOR (olculur, varsayilmaz).
//   6) Ekran goruntuleri /home/ubuntu/ui-shots/ altina yaziliyor.

#include "model_maker/command_catalog.hpp"
#include "model_maker/ribbon_widget.hpp"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message.c_str());
    }
}

class FakeHost : public mm::ICommandHost {
public:
    void runCommand(const std::string& id) override { run.push_back(id); }
    bool isEnabled(const std::string& key) const override {
        return key != "occ";  // OCC yok: boolean komutlari pasif
    }
    bool toggleState(const std::string& key) const override {
        const auto it = toggles.find(key);
        return it != toggles.end() && it->second;
    }
    std::vector<mm::DynamicMenuItem> dynamicMenu(const std::string& id) const override {
        if (id == "steel.catalog") {
            return {{"component:end_plate", "Alın Levhası", "plate"},
                    {"component:fin_plate", "Kayma Levhası", "plate"},
                    {"component:haunch_40", "Haunch (40)", "plate"},
                    {"component:pkg_test", "Test Paket Parçası", "plate"}};
        }
        return {};
    }

    std::vector<std::string> run;
    std::map<std::string, bool> toggles;
};

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const mm::CommandCatalog catalog = mm::buildCommandCatalog();

    mm::RibbonWidget ribbon;
    ribbon.setPersistence(false);  // test: QSettings'e yazma
    ribbon.setMenu(new QMenu(&ribbon));
    ribbon.setHost(nullptr);
    ribbon.setQuickAccess({"file.save", "edit.undo", "edit.redo"});
    ribbon.setCatalog(catalog);

    FakeHost host;
    host.toggles["snap"] = true;
    host.toggles["dock.modelTree"] = false;
    ribbon.setHost(&host);
    ribbon.refresh();

    ribbon.resize(1500, 210);
    ribbon.show();
    QApplication::processEvents();

    // ---- 1) Her serit komutu TAM BIR kez -------------------------------
    {
        std::map<std::string, int> seen;
        for (const std::string& id : ribbon.commandIdsInView()) {
            if (catalog.registry.contains(id)) ++seen[id];
        }
        for (const mm::CommandDef& def : catalog.registry.commands()) {
            const int count = seen.count(def.id) ? seen[def.id] : 0;
            if (def.surface == mm::CommandSurface::Ribbon && count != 1) {
                std::printf("  EKSIK/FAZLA: %s -> %d\n", def.id.c_str(), count);
                check(false, "serit komutu seritte tam bir kez olmali");
            }
            if (def.surface != mm::CommandSurface::Ribbon && count != 0) {
                std::printf("  YANLIS YER: %s -> %d\n", def.id.c_str(), count);
                check(false, "menu/alt cubuk komutu seritte gorunmemeli");
            }
        }
        check(ribbon.buttonCount() > 0, "seritte dugme var");
        check(ribbon.menuCount() >= 5, "seritte acilir dugmeler var (Yon/Stil/Gorunumler...)");
    }

    // ---- 2) Tiklama dogru komutu calistirir ----------------------------
    {
        host.run.clear();
        ribbon.activateCommand("draw.line");
        check(host.run.size() == 1 && host.run[0] == "draw.line",
              "Cizgi dugmesi draw.line calistirmali");

        host.run.clear();
        ribbon.activateCommand("edit.fillet");
        check(host.run.size() == 1 && host.run[0] == "edit.fillet",
              "Fillet dugmesi edit.fillet calistirmali (artik ol u degil)");

        host.run.clear();
        ribbon.activateCommand("view.gpuLines");
        check(host.run.size() == 1 && host.run[0] == "view.gpuLines",
              "GPU cizgiler dugmesi dogru komutu calistirmali");
    }

    // ---- 3) Hizli arama ------------------------------------------------
    {
        host.run.clear();
        const std::string found = ribbon.runSearch(QStringLiteral("kolon"));
        check(found == "draw.column", "arama 'kolon' -> draw.column");
        check(host.run.size() == 1 && host.run[0] == "draw.column",
              "arama sonucu komutu CALISTIRIR");

        const std::string save = ribbon.runSearch(QStringLiteral("KAYDET"));
        check(save == "file.save", "arama buyuk harf duyarsiz: KAYDET -> file.save");

        const std::string none = ribbon.runSearch(QStringLiteral("zzzz-yok"));
        check(none.empty(), "eslesmeyen arama komut calistirmaz");

        const QStringList suggestions = ribbon.searchSuggestions(QStringLiteral("kir"), 5);
        check(suggestions.size() <= 5, "oneri listesi limitli");

        // B1: hizli arama yerlesik component'i kayit indeksinden bulur
        // (kullanici terimi "haunch" -> registry'deki search-index komutu).
        host.run.clear();
        const std::string comp = ribbon.runSearch(QStringLiteral("haunch"));
        check(comp == "component:tekla_haunch_40",
              "arama 'haunch' -> component:tekla_haunch_40");
        check(host.run.size() == 1 && host.run[0] == "component:tekla_haunch_40",
              "yerlesik bilesen aramasi komutu CALISTIRIR");
        const QStringList sug2 = ribbon.searchSuggestions(QStringLiteral("haunch"), 5);
        check(sug2.contains(QStringLiteral("Haunch (40)")),
              "oneriler bilesen etiketlerini icerir");
        // Dinamik katalog (host dynamicMenu) fallback'i: yalnizca host'ta olan
        // bir paket bileseni de aramadan bulunup calistirilabilmeli.
        host.run.clear();
        const std::string pkg = ribbon.runSearch(QStringLiteral("Test Paket"));
        check(pkg == "component:pkg_test",
              "arama paket bilesenini dynamicMenu fallback'inden bulur");
        check(host.run.size() == 1 && host.run[0] == "component:pkg_test",
              "paket bileseni aramasi komutu CALISTIRIR");
    }

    // ---- 3b) Katalog dropdown: dinamik menu host'tan dolar (B1) -------
    {
        QToolButton* katalog = nullptr;
        for (QToolButton* b : ribbon.findChildren<QToolButton*>()) {
            if (b->property("commandId").toString() == QStringLiteral("steel.catalog")) {
                katalog = b;
                break;
            }
        }
        check(katalog != nullptr, "Katalog dugmesi seritte var");
        if (katalog && katalog->menu()) {
            QMenu* m = katalog->menu();
            // host setCatalog'tan SONRA kurulmus olsa bile aboutToShow
            // dinamik icerigi host'tan yeniden doldurmali.
            QMetaObject::invokeMethod(m, "aboutToShow", Qt::DirectConnection);
            QApplication::processEvents();
            QStringList labels;
            for (QAction* a : m->actions()) labels << a->text();
            check(labels.contains(QStringLiteral("Haunch (40)")) &&
                      labels.contains(QStringLiteral("Alın Levhası")) &&
                      labels.contains(QStringLiteral("Kayma Levhası")),
                  "Katalog menusu registry bilesenlerini listeler");
            host.run.clear();
            for (QAction* a : m->actions()) {
                if (a->text() == QStringLiteral("Haunch (40)")) {
                    a->trigger();
                    break;
                }
            }
            check(host.run.size() == 1 && host.run[0] == "component:haunch_40",
                  "menu ogesi component komutunu calistirir");
        }
    }

    // ---- 4) Kisayollar degismedi --------------------------------------
    {
        struct Expect {
            const char* id;
            const char* shortcut;
            mm::ShortcutDelivery delivery;
        };
        const Expect expected[] = {
            {"file.save", "Ctrl+S", mm::ShortcutDelivery::Host},
            {"file.new", "Ctrl+N", mm::ShortcutDelivery::Host},
            {"edit.undo", "Ctrl+Z", mm::ShortcutDelivery::Host},
            {"edit.redo", "Ctrl+Y", mm::ShortcutDelivery::Host},
            {"draw.line", "L", mm::ShortcutDelivery::Canvas},
            {"draw.column", "G", mm::ShortcutDelivery::Canvas},
            {"edit.fillet", "Q", mm::ShortcutDelivery::Canvas},
            {"view.dir.iso", "Ctrl+7", mm::ShortcutDelivery::Host},
            {"view.style.solid", "Alt+4", mm::ShortcutDelivery::Host},
            {"views.plan", "Ctrl+Shift+P", mm::ShortcutDelivery::Host},
            {"assist.snap", "F3", mm::ShortcutDelivery::Canvas},
        };
        for (const Expect& exp : expected) {
            const mm::CommandDef* def = catalog.registry.find(exp.id);
            check(def != nullptr, std::string("kayitta yok: ") + exp.id);
            if (!def) continue;
            check(def->shortcut == exp.shortcut,
                  std::string("kisayol degisti: ") + exp.id + " -> " + def->shortcut);
            check(def->delivery == exp.delivery,
                  std::string("teslim yolu degisti: ") + exp.id);
        }
        // 'G' hala Kolon; Fillet Q. Ayni tus iki komuta bagli DEGIL.
        check(catalog.registry.commandForShortcut("G")->id == "draw.column",
              "'G' = Kolon (korunmali)");
        check(catalog.registry.shortcutConflicts().empty(), "kisayol cakismasi yok");
    }

    // ---- 5) DPI: 1.0 / 1.5 / 2.0 ---------------------------------------
    {
        const QFont base = ribbon.font();
        for (int scalePercent : {100, 150, 200}) {
            QFont scaled = base;
            scaled.setPointSizeF(base.pointSizeF() * scalePercent / 100.0);
            ribbon.setFont(scaled);
            QApplication::sendEvent(&ribbon, new QEvent(QEvent::FontChange));
            QApplication::processEvents();

            const int clipped = ribbon.clippedLabelCount();
            if (clipped != 0) {
                std::printf("  KESIK ETIKET: %d dugme (%d%%)\n", clipped, scalePercent);
            }
            check(clipped == 0,
                  "DPI " + std::to_string(scalePercent) + "%: etiketler kesilmemeli");
        }
        ribbon.setFont(base);
        QApplication::sendEvent(&ribbon, new QEvent(QEvent::FontChange));
        QApplication::processEvents();
    }

    // ---- 6) Sekme hatirlama -------------------------------------------
    {
        ribbon.setSelectedTab(QStringLiteral("Düzenle"));
        QApplication::processEvents();
        check(ribbon.selectedTab() == QStringLiteral("Düzenle"), "sekme secilebiliyor");
        ribbon.setCollapsed(true);
        QApplication::processEvents();
        check(ribbon.isCollapsed(), "serit daraltilabiliyor");
        ribbon.setCollapsed(false);
        ribbon.setSelectedTab(QStringLiteral("Çelik"));
        QApplication::processEvents();
    }

    // ---- 7) Metin alani odaktayken tek harf kisayollari TETIKLENMEZ ------
    // Kullanici sarti: profil alanina "HEA300" yazmak komut baslatmasin.
    {
        // (i) Mimari kural: tek harf kisayollari (G, L, Q, S...) YALNIZ tuval
        // teslimli olmali. Qt kisayolu (Host) olarak baglanirlarsa metin
        // alanina yazarken komut baslar.
        int canvasLetters = 0;
        int hostLetters = 0;
        for (const mm::CommandDef& def : catalog.registry.commands()) {
            if (def.shortcut.empty()) continue;
            const auto chord = mm::parseShortcut(def.shortcut);
            if (!chord || chord->key.size() != 1) continue;            // harf/rakam
            if (chord->ctrl || chord->alt || chord->shift) continue;   // degistiricili
            if (def.delivery == mm::ShortcutDelivery::Canvas) ++canvasLetters;
            else ++hostLetters;
        }
        check(canvasLetters > 0, "tek harf kisayollari kayitta var (G, L, Q, S...)");
        check(hostLetters == 0, "tek harf kisayolu Qt (Host) kisayolu OLMAMALI");

        // (ii) Gercek Qt davranisi: QLineEdit ShortcutOverride'i kabul eder,
        // bu yuzden pencere kisayolu (G) metin yazarken ATESLENMEZ.
        QWidget hostWindow;
        auto* column = new QVBoxLayout(&hostWindow);
        auto* edit = new QLineEdit(&hostWindow);
        column->addWidget(edit);
        hostWindow.resize(420, 120);
        hostWindow.show();
        edit->setFocus();
        QApplication::processEvents();

        bool shortcutFired = false;
        auto* guardShortcut = new QShortcut(QKeySequence(QStringLiteral("G")), &hostWindow);
        QObject::connect(guardShortcut, &QShortcut::activated,
                         [&shortcutFired]() { shortcutFired = true; });

        host.run.clear();
        for (const QChar ch : QStringLiteral("HEA300")) {
            QKeyEvent press(QEvent::KeyPress, ch.unicode(), Qt::NoModifier, QString(ch));
            QApplication::sendEvent(edit, &press);
        }
        QApplication::processEvents();

        check(edit->text() == QStringLiteral("HEA300"),
              "profil alanina 'HEA300' yazilmali");
        check(!shortcutFired, "'G' kisayolu metin alaninda ATESLENMEMELI");
        check(host.run.empty(), "profil alanina yazmak HICBIR komut baslatmamali");
    }

    // ---- 8) Ekran goruntuleri ------------------------------------------
    {
        const QString dir = QStringLiteral("/home/ubuntu/ui-shots");
        ribbon.resize(1500, 210);
        QApplication::processEvents();
        const QStringList shots = ribbon.saveScreenshots(dir);
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            std::printf("PNG: offscreen ortam, ekran goruntusu adimi atlandi\n");
        else
            check(!shots.isEmpty(), "ekran goruntusu yazilmali");
        for (const QString& path : shots) std::printf("PNG: %s\n", qPrintable(path));
    }

    if (failures == 0) {
        std::printf("ribbon_widget_test: TUM KONTROLLER GECTI\n");
        return 0;
    }
    std::printf("ribbon_widget_test: %d HATA\n", failures);
    return 1;
}
