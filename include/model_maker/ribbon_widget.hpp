#pragma once

// SERIT (RIBBON) — platformdan BAGIMSIZ Qt widget'i (UI-2).
//
// TASARIM KURALI (kullanici): qt_main_window.cpp yalniz Windows'ta derlenir ve
// Linux'ta calistirilamaz. Bu yuzden serit burada AYRI bir widget olarak durur
// ve Application'a DOKUNMAZ: komut calistirma/etkinlik/durum sorgulari
// ICommandHost arayuzunden gecer. Boylece widget Linux'ta
// QT_QPA_PLATFORM=offscreen ile DERLENIR, TEST EDILIR ve ekran goruntusu
// alinir. qt_main_window.cpp yalniz host adaptorunu yazar (ince baglanti).
//
// Icerik TEK KAYNAKTAN gelir: CommandCatalog (command_registry.hpp) —
// komut tanimlari + serit plani. Widget'ta elle dugme/komut EKLENMEZ.

#include "model_maker/command_registry.hpp"

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

class QCompleter;
class QEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QTabWidget;
class QToolButton;
class QVBoxLayout;

namespace mm {

// Serit ile uygulama arasindaki INCE arayuz.
//   runCommand  : dugmeye tiklandi / kisayol calisti
//   isEnabled   : komutun etkinlik kosulu (CommandDef::enabledKey doluysa)
//   toggleState : ac-kapa komutunun guncel durumu (CommandDef::toggleKey)
//   dynamicMenu : plan cocugu olmayan acilir icerik (component katalogu gibi
//                 CALISMA ANINDA registry'den uretilen komutlar)
// Dinamik acilir menu ogesi: id + gorunen ad (komut katalogda YOK; ornegin
// component katalogu calisma aninda registry'den uretilir).
struct DynamicMenuItem {
    std::string id;
    std::string label;
    std::string icon;
};

class ICommandHost {
public:
    virtual ~ICommandHost() = default;
    virtual void runCommand(const std::string& id) = 0;
    virtual bool isEnabled(const std::string& id) const {
        (void)id;
        return true;
    }
    virtual bool toggleState(const std::string& id) const {
        (void)id;
        return false;
    }
    virtual std::vector<DynamicMenuItem> dynamicMenu(const std::string& id) const {
        (void)id;
        return {};
    }
};

class RibbonWidget : public QWidget {
    Q_OBJECT

public:
    explicit RibbonWidget(QWidget* parent = nullptr);
    ~RibbonWidget() override;

    // Katalog (kayit + serit plani). Widget kataloğu KOPYALAR; omru kendisine aittir.
    void setCatalog(const CommandCatalog& catalog);
    void setHost(ICommandHost* host);
    // ≡ menusu (Dosya/Pencere komutlari burada) — sahiplik cagiranda kalir.
    void setMenu(QMenu* menu);
    // Hizli erisim dugmeleri (komut id'leri).
    void setQuickAccess(const std::vector<std::string>& ids);
    // Oturumlar arasi hatirlama (secili sekme + daraltilmis). Testte kapatilir.
    void setPersistence(bool enabled, const QString& group = QStringLiteral("Ui/Ribbon"));

    QString selectedTab() const;
    void setSelectedTab(const QString& title);
    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

    // Etkinlik / ac-kapa durumlarini yeniden okur (host degistiginde).
    void refresh();

    // ---- Test/teshis yuzeyi -------------------------------------------
    std::size_t buttonCount() const;
    std::vector<std::string> commandIdsInView() const;
    std::size_t menuCount() const;
    // Dugmeye tiklamakla AYNI yolu kullanir (test: dogru komut calisti mi).
    void activateCommand(const std::string& id);
    // Hizli arama: metni cozer, EN IYI eslesmeyi calistirir; calisan id'yi doner.
    std::string runSearch(const QString& text);
    QStringList searchSuggestions(const QString& text, int limit = 12) const;
    // GT-1: arama kutusunun GUNCEL metni (durum JSON'u).
    QString searchText() const;
    // Etiketi kutusuna sigmayan dugme sayisi (DPI testi: 1.0/1.5/2.0).
    int clippedLabelCount() const;
    // Ekran goruntuleri (rapor): sekmeler + daraltilmis + arama.
    QStringList saveScreenshots(const QString& directory);

signals:
    void commandTriggered(const QString& id);

protected:
    // DPI/yazi tipi degisiminde dugme genislikleri YENIDEN hesaplanir
    // (1.0/1.5/2.0 DPI'da etiket kesilmemesi bu yolun testidir).
    void changeEvent(QEvent* event) override;

private:
    struct Entry {
        std::string id;
        QToolButton* button{nullptr};
        bool isMenu{false};
        bool quickAccess{false};  // hizli erisim seridindeki dugme (serit plani DEGIL)
    };

    void buildUi();
    QWidget* buildTabPage(const PlanTab& tab);
    QToolButton* makeButton(const std::string& id, bool large);
    QToolButton* makeMenuButton(const PlanItem& node, bool large);
    void performCommand(const std::string& id);
    void restoreState();
    void storeState();
    std::vector<std::string> collectIds(const std::vector<PlanItem>& items) const;

    CommandCatalog catalog_;
    ICommandHost* host_{nullptr};
    QMenu* menu_{nullptr};
    QTabWidget* tabs_{nullptr};
    QWidget* content_{nullptr};
    QLineEdit* search_{nullptr};
    QCompleter* completer_{nullptr};
    QToolButton* collapseBtn_{nullptr};
    QWidget* quickBar_{nullptr};
    std::vector<Entry> entries_;
    std::vector<std::string> quickAccess_;
    bool persistence_{true};
    QString settingsGroup_{QStringLiteral("Ui/Ribbon")};
    bool collapsed_{false};
    bool building_{false};
};

// Ikon: bizim (tek renk, vektorel cizim). `id` CommandDef::icon alanidir;
// bilinmeyen id icin notr bir cerceve cizilir (elle resim YOK).
QIcon commandIcon(const std::string& id, const QColor& color = QColor(0xE8, 0xE8, 0xE8));

} // namespace mm
