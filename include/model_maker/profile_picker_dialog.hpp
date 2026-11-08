#pragma once

#include "model_maker/profile_database.hpp"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QTableWidget;

namespace mm {

struct SteelProfile;

// UI-4: Profil secim penceresi ("..." dugmesi).
//
// Kurallar (INBOX UI-34 / ROADMAP-UI UI-4):
// - Solda aileye gore agac (HEA, HEB, HEM, IPE, UPN, kutu, boru… katalogda
//   ne varsa), dogal (sayisal farkindalikli) sirada.
// - Ustte Filtre (`*` joker, or. `HE*3*`); aile gruplarini ve agaci filtreler.
// - Profil adi alani (yazilabilir, agacla senkron: agac -> alan secimde,
//   alan -> agac eslesen kaydi secer; eslesme yoksa secim temizlenir).
// - Sagda kesit resmi KENDI sematik motorumuzla (olcu harfleri h b tw tf r)
//   + ozellik tablosu (Ozellik / Sembol / Deger / Birim; katalogda ne varsa).
// - Altta Tamam / Uygula / Iptal. Uygula alana yazar, pencere acik kalir.
// - Tekla ikon/resim/marka YOK. Platformdan bagimsiz Qt widget'i:
//   Application'a dokunmaz; katalog setProfiles() ile disaridan verilir.
class ProfilePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfilePickerDialog(QWidget* parent = nullptr);

    // Katalog bir kez kurulur (boyutlar + adlar; dogal sira burada uygulanir).
    void setProfiles(const std::vector<SteelProfile>& profiles);
    // Acilista alana konan ad (paneldeki mevcut deger).
    void setInitialName(const QString& name);
    // Secili profil adi (canonical; secim yoksa bos). Iptal'de bile son
    // TAMAM/UYGULA secimi doner (arayan kisi kendisi kontrol eder).
    QString selectedProfile() const { return selectedProfile_; }
    // Alandaki ham metin (canonical degil).
    QString profileName() const;
    // Filtre alanina odaklanir (hizli arama).
    void focusFilter();

signals:
    // UYGULA / TAMAM: secilen canonical ad (bos olabilir = secim yok).
    void profileApplied(const QString& name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildTree();
    void applyFilter();
    void syncFromTree();
    void syncToTree();
    void selectProfile(const QString& canonicalName);
    const SteelProfile* currentProfile() const;
    void populateProperties(const SteelProfile* profile);
    void onOkClicked();
    void onApplyClicked();

    std::vector<SteelProfile> profiles_;
    QLineEdit* filterEdit_{};
    QLineEdit* nameEdit_{};
    QTreeWidget* tree_{};
    QCheckBox* showAllCheck_{};
    QCheckBox* showDetailsCheck_{};
    QWidget* sectionPreview_{};
    QLabel* sectionLabel_{};
    QTableWidget* propsTable_{};
    QPushButton* okButton_{};
    QPushButton* applyButton_{};
    QPushButton* cancelButton_{};
    QString selectedProfile_;
    bool suppressSync_{};
};

// UI-4: Malzeme secim penceresi ("..." dugmesi) — ayni yapida BASIT liste.
// Filtre ustte, liste altta; Tamam / Uygula / Iptal. Uygula alana yazar,
// pencere acik kalir.
class MaterialPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit MaterialPickerDialog(QWidget* parent = nullptr);

    void setMaterials(const QStringList& items);
    void setInitialName(const QString& name);
    QString selectedMaterial() const { return selectedMaterial_; }
    void focusFilter();

signals:
    void materialApplied(const QString& name);

private:
    void applyFilter();
    void onOkClicked();
    void onApplyClicked();

    QStringList materials_;
    QLineEdit* filterEdit_{};
    QListWidget* list_{};
    QPushButton* okButton_{};
    QPushButton* applyButton_{};
    QString selectedMaterial_;
};

} // namespace mm
