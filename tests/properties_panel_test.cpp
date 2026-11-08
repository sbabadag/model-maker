// OZELLIKLER PANELI (UI-3a) — Qt offscreen testi. Linux + Windows, Qt6 varsa.
//
// Kapsam (INBOX UI-34 kesit 1 kabul kriterleri):
//   1) Baslik "<tip> (N secili)" ve bolumler (Genel/Konum/Gorunum/Kolon) var;
//      bolum basligi tiklaninca icerik gizlenir/acilir (daraltilabilir).
//   2) Profil katalogu BIR KEZ kurulur; refresh (setProfile) completer
//      modelini YENIDEN KURMAZ (eski "clear() ile liste yeniden kuruluyor"
//      hatasinin sozlesmesi — kullanicinin sectigi deger kaybolmaz).
//   3) Setter'lar (refresh) kirli bayragi SET ETMEZ.
//   4) Gecersiz profil/malzeme: validator false doner, ENTER uygulamaz,
//      alan kirli kalir; gecerli ENTER uygular ve kirliyi temizler.
//   5) UYGULA yalniz kirli alan varken aktif; clearDirtyFlags sifirlar.
//   6) Sinif (renk kutulu) secimler + kolon kotlari degerleri dogru doner.
//   7) KOLON YERLESTIR ve onColumnLevelsEdit/Enter callback'leri calisir.
//   8) "..." dugmeleri UI-4'e kadar pasif.

#include "model_maker/properties_panel.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message.c_str());
    }
}

// Gercek kullanici yazimini taklit eder: QLineEdit::keyPressEvent ->
// textEdited sinyali (setText bunu URETMEZ).
void typeText(QWidget* widget, const QString& text) {
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(widget, &press);
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(widget, &release);
    }
}

void pressReturn(QWidget* widget) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    mm::PropertiesPanel panel;
    panel.resize(300, 900);
    panel.show();

    // 1) Baslik + bolumler (daraltilabilir).
    panel.setTitle(QStringLiteral("Kiriş (1 seçili)"));
    QLabel* title = panel.findChild<QLabel*>(QStringLiteral("propsTitle"));
    check(title && title->text() == QStringLiteral("Kiriş (1 seçili)"),
          "baslik setTitle ile guncelleniyor");
    QToolButton* genelHeader =
        panel.findChild<QToolButton*>(QStringLiteral("sectionGenel"));
    check(genelHeader && genelHeader->isCheckable(),
          "Genel bolum basligi checkable");
    QWidget* genelBody =
        panel.findChild<QWidget*>(QStringLiteral("sectionGenelBody"));
    check(genelBody && genelBody->isVisible(), "Genel icerigi baslangicta acik");
    if (genelHeader && genelBody) {
        genelHeader->setChecked(false);
        check(!genelBody->isVisible(),
              "bolum basligi tiklaninca icerik gizleniyor (daraltilabilir)");
        genelHeader->setChecked(true);
        check(genelBody->isVisible(), "bolum yeniden aciliyor");
    }

    // 2) Profil katalogu bir kez; refresh modeli yeniden kurmaz.
    const QStringList catalog = {QStringLiteral("HEB240"), QStringLiteral("IPE300")};
    panel.setProfileCatalog(catalog);
    QCompleter* completer = panel.profileEdit()->completer();
    check(completer && completer->model() &&
              completer->model()->rowCount() == 2,
          "profil tamamlayici katalogu bir kez kuruldu (2 oge)");
    QAbstractItemModel* firstModel = completer->model();
    panel.setProfile(QStringLiteral("HEB240"));
    panel.setProfile(QStringLiteral("IPE300"));
    check(completer->model() == firstModel,
          "refresh profil modelini YENIDEN KURMUYOR (kullanici degeri kaybolmaz)");

    // 3) Setter'lar (refresh) kirletmez.
    panel.setProfile(QStringLiteral("HEB240"));
    check(!panel.isProfileDirty() && !panel.isAnyDirty(),
          "setProfile refresh'i kirli bayragi SET ETMEZ");

    // 4) Validator: gecersiz -> ENTER uygulamaz, kirli kalir;
    //    gecerli -> uygular ve kirliyi temizler.
    bool applied = false;
    QString appliedText;
    panel.onProfileEnter = [&](const QString& t) {
        applied = true;
        appliedText = t;
    };
    panel.validateProfile = [&catalog](const QString& t) {
        return t.isEmpty() || catalog.contains(t);
    };
    QLineEdit* profileEdit = panel.profileEdit();
    profileEdit->clear();
    typeText(profileEdit, QStringLiteral("XXX999"));
    pressReturn(profileEdit);
    check(!applied, "gecersiz profil ENTER ile UYGULANMAZ");
    check(panel.isProfileDirty(),
          "gecersiz profil alani kirli kalir (UYGULA'da tekrar reddedilir)");
    profileEdit->clear();
    typeText(profileEdit, QStringLiteral("HEB240"));
    pressReturn(profileEdit);
    check(applied && appliedText == QStringLiteral("HEB240"),
          "gecerli profil ENTER ile uygulanir");
    check(!panel.isProfileDirty(), "uygulanan profil kirli bayragi temizlenir");

    // 5) UYGULA: kirli alan varken aktif; temizleyince pasif.
    QPushButton* applyButton =
        panel.findChild<QPushButton*>(QStringLiteral("propsApply"));
    check(applyButton && !applyButton->isEnabled(),
          "UYGULA temiz panelde pasif (A kurali)");
    QLineEdit* nameEdit = panel.findChild<QLineEdit*>(QStringLiteral("propsName"));
    check(nameEdit != nullptr, "Ad alani var");
    typeText(nameEdit, QStringLiteral("K1"));
    check(panel.isNameDirty() && panel.isAnyDirty(),
          "kullanici yazimi Ad alanini kirli isaretler");
    bool applyClicked = false;
    panel.onApplyClicked = [&]() { applyClicked = true; };
    applyButton->click();
    check(applyClicked, "UYGULA tiklaninca callback calisir");
    panel.clearDirtyFlags();
    check(!panel.isAnyDirty() && !applyButton->isEnabled(),
          "clearDirtyFlags sonrasi UYGULA pasif");

    // 6) Sinif secimleri (renk kutulu) + kolon kotlari.
    panel.setClassChoices({QStringLiteral("Kırmızı"), QStringLiteral("Mavi")},
                          {QColor(QStringLiteral("#C0392B")),
                           QColor(QStringLiteral("#2E86C1"))});
    QComboBox* classCombo =
        panel.findChild<QComboBox*>(QStringLiteral("propsClass"));
    check(classCombo && classCombo->count() == 2,
          "Sinif listesi renk paletinden kuruldu");
    panel.setClassIndex(1);
    check(panel.classIndex() == 1, "Sinif secimi okunuyor");
    panel.setClassIndex(-1);
    check(panel.classIndex() == -1, "belirsiz sinif -1");

    panel.setColumnLevels(7000.0, 0.0);
    check(panel.columnTop() == 7000.0 && panel.columnBottom() == 0.0,
          "kolon kotlari okunuyor");

    // 7) Kolon callback'leri: refresh tetiklemez, dugme tetikler.
    int levelEdits = 0;
    bool levelEnter = false;
    panel.onColumnLevelsEdit = [&](double, double) { ++levelEdits; };
    panel.onColumnLevelsEnter = [&]() { levelEnter = true; };
    bool columnPlace = false;
    panel.onColumnPlaceClicked = [&]() { columnPlace = true; };
    QPushButton* columnBtn =
        panel.findChild<QPushButton*>(QStringLiteral("propsColumnBtn"));
    columnBtn->click();
    check(columnPlace, "KOLON YERLESTIR callback calisir");
    check(levelEdits == 0 && !levelEnter,
          "setColumnLevels (refresh) callback TETIKLEMEZ");

    // 8) "..." dugmeleri UI-4 ile aktif: browse callback cagrilir, donen
    //    canonical ad alana yazilir + kirli isaretlenir; bos donus (Iptal)
    //    alani degistirmez.
    QToolButton* profileButton =
        panel.findChild<QToolButton*>(QStringLiteral("propsProfileButton"));
    check(profileButton && profileButton->isEnabled(),
          "profil \"...\" dugmesi UI-4 ile aktif");
    QString browsed;
    panel.onProfileBrowse = [&]() { return browsed; };
    browsed = QStringLiteral("IPE300");
    profileButton->click();
    check(panel.profileText() == QStringLiteral("IPE300") &&
              panel.isProfileDirty(),
          "browse secimi alana yazilir + kirli isaretlenir");
    browsed = QString();
    profileButton->click();
    check(panel.profileText() == QStringLiteral("IPE300"),
          "Iptal (bos donus) alani degistirmez");
    panel.clearDirtyFlags();
    panel.applyProfileFromPicker(QStringLiteral("HEB240"));
    check(panel.profileText() == QStringLiteral("HEB240") &&
              panel.isProfileDirty(),
          "picker UYGULA panele yazar + kirli (A kurali akisi)");
    panel.applyProfileFromPicker(QString());
    check(panel.profileText() == QStringLiteral("HEB240"),
          "bos picker uygulamasi alani degistirmez");

    if (failures == 0)
        std::printf("OK: properties_panel_test — hepsi gecti\n");
    else
        std::printf("SONUC: %d hata\n", failures);
    return failures == 0 ? 0 : 1;
}
