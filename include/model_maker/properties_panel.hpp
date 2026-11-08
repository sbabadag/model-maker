#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QToolButton;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QCompleter;
class QStringListModel;

namespace mm {

// UI-3a: Tekla tarzi OZELLIKLER PANELI iskeleti (sag dock'ta yerlesik).
//
// Kurallar (INBOX UI-34 / ROADMAP-UI UI-3):
// - Platformdan bagimsiz Qt widget'i: Application'a DOKUNMAZ. Degerler
//   setter'larla konur (500 ms refresh), kullanici degisiklikleri callback /
//   getter + kirli bayraklariyla disari bildirilir; qt_main_window ince
//   baglantidir (MESAJ-14).
// - Daraltilabilir bolumler: Genel (Ad, Profil [+ "..."], Malzeme [+ "..."],
//   Sinif [renk kutulu], Yuzey islemi), Konum (Donus — modeldeki mevcut alan;
//   duzlemde/derinlik icin modelde karsilik YOK, uydurulmadi), Gorunum
//   (Katman, Cizgi Tipi — mevcut islevsellik), Kolon (Ust/Alt kot + Uzunluk).
// - Alt cubuk: UYGULA (A kurali: tek dugme; yalniz degisen alanlar uygulanir;
//   tek geri alma adimi) + KOLON YERLESTIR.
// - Profil/malzeme elle yazilir, buyuk/kucuk harf duyarsiz, otomatik
//   tamamlama; katalogda yoksa alan KIRMIZI olur ve uygulanmaz
//   (validator callback'i disaridan verilir).
// - Eski "Profil (aktif)" acilir listesi ve "Profilsiz (duz cizgi)"
//   secenegi KALKTI (B6: Cizgi zaten profili yok sayar; Kolon/Kiris profil
//   zorunlu).
// - "..." dugmeleri (UI-4): profil/malzeme secim penceresi — katalog
//   disaridan (Application); panel onProfileBrowse/onMaterialBrowse
//   callback'iyle acar, donen canonical adi alana yazar.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // ---- Disari: refresh (qt_main_window 500 ms timer'i cagirir) ----
    void setTitle(const QString& text);               // "<tip> (N secili)"
    void setInfo(const QString& text);                // kaynak/montaj bilgisi
    void setLengthLabel(const QString& text);         // salt okunur uzunluk
    void setProfileCatalog(const QStringList& names); // bir kez (completer)
    void setProfile(const QString& text);             // secili/aktif profil
    void setProfileValid(bool valid);                 // false = kirmizi
    void setMaterialChoices(const QStringList& items);// bir kez (completer)
    void setMaterial(const QString& text);
    void setMaterialValid(bool valid);
    // GT-1: profil alaninin kirmizi (gecersiz) durumu.
    bool isProfileValid() const noexcept { return profileValid_; }
    void setName(const QString& text);                // coklu secimde bos
    void setFinishChoices(const QStringList& items);  // bir kez
    void setFinish(const QString& text);              // coklu secimde bos
    void setClassChoices(const QStringList& labels,
                         const QList<QColor>& colors);// bir kez (renk kutulu)
    void setClassIndex(int index);                    // -1 = belirsiz
    void setLayerChoices(const QStringList& items);
    void setLayer(const QString& text);
    void setLineTypeChoices(const QStringList& items);
    void setLineType(const QString& text);
    void setRotation(double degrees);
    void setColumnLevels(double top, double bottom);
    void setApplyEnabled(bool enabled);

    // ---- Kullanici eylemleri / getter'lar ----
    void focusProfileEdit();        // X kisa yolu: odak + tamamlama popup
    QLineEdit* profileEdit() const noexcept { return profileEdit_; }
    QString profileText() const;    // ham metin (normalizasyon disarida)
    QString materialText() const;
    QString name() const;
    QString finish() const;
    int classIndex() const;
    double rotation() const;
    double columnTop() const;
    double columnBottom() const;

    // Kirli bayraklari: UYGULA yalniz kirli alanlari disari bildirir
    // (coklu secimde degistirilmeyen alan yazilmaz — A kurali).
    bool isProfileDirty() const noexcept { return profileDirty_; }
    bool isMaterialDirty() const noexcept { return materialDirty_; }
    bool isNameDirty() const noexcept { return nameDirty_; }
    bool isFinishDirty() const noexcept { return finishDirty_; }
    bool isClassDirty() const noexcept { return classDirty_; }
    bool isRotationDirty() const noexcept { return rotationDirty_; }
    bool isColumnDirty() const noexcept { return columnDirty_; }
    bool isAnyDirty() const noexcept;
    void clearDirtyFlags();

    // ---- Callback'ler (qt_main_window baglar; Application'a dokunmaz) ----
    std::function<void(const QString&)> onProfileEnter;   // ENTER/tamamlama
    std::function<void(const QString&)> onMaterialEnter;  // ENTER/tamamlama
    std::function<void()> onApplyClicked;                 // UYGULA
    std::function<void()> onColumnPlaceClicked;           // KOLON YERLESTIR
    std::function<void(double, double)> onColumnLevelsEdit; // sonraki kolon
    std::function<void()> onColumnLevelsEnter;            // kolon kot ENTER
    std::function<void(int)> onClassPicked;
    std::function<void(const QString&)> onLayerPicked;
    std::function<void(int)> onLineTypePicked;
    // Profil/malzeme alanlari yazildikca cagrilir: true = katalogda var
    // (bos da gecerli); false = kirmizi cerceve. Panelin katalog bilgisi
    // yok — kontrol disarida.
    std::function<bool(const QString&)> validateProfile;
    std::function<bool(const QString&)> validateMaterial;
    // UI-4: "..." dugmesi. Secim penceresini acan taraf DISARIDIR (panel
    // katalog bilmez); callback canonical adi dondurur (Iptal = bos), panel
    // alana yazar + kirli isaretler + dogrulama calistirir.
    std::function<QString()> onProfileBrowse;
    std::function<QString()> onMaterialBrowse;

    // UI-4 secim penceresinin UYGULA dugmesi (pencere acik kalir): alana
    // yazar + kirli isaretler; kullanici panelde UYGULA'ya basar.
    void applyProfileFromPicker(const QString& canonicalName);
    void applyMaterialFromPicker(const QString& canonicalName);

private:
    QWidget* addSection(const QString& title, const QString& objectName);
    QWidget* fieldRow(QWidget* parent, const QString& caption, QWidget* control);
    void markProfileDirty();
    void markMaterialDirty();
    void updateApplyEnabled();

    QLabel* title_{};
    QLabel* info_{};
    QLineEdit* nameEdit_{};
    QLineEdit* profileEdit_{};
    QCompleter* profileCompleter_{};
    QToolButton* profileButton_{};
    QLineEdit* materialEdit_{};
    QCompleter* materialCompleter_{};
    QToolButton* materialButton_{};
    QComboBox* classCombo_{};
    QComboBox* finishCombo_{};
    QComboBox* layerCombo_{};
    QComboBox* lineTypeCombo_{};
    QDoubleSpinBox* rotationSpin_{};
    QDoubleSpinBox* columnTopSpin_{};
    QDoubleSpinBox* columnBottomSpin_{};
    QLabel* lengthLabel_{};
    QPushButton* applyButton_{};
    QPushButton* columnPlaceButton_{};

    bool profileDirty_{};
    bool materialDirty_{};
    bool profileValid_{true};
    bool nameDirty_{};
    bool finishDirty_{};
    bool classDirty_{};
    bool rotationDirty_{};
    bool columnDirty_{};
};

} // namespace mm
