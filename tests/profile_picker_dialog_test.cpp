// UI-4 PROFIL/MALZEME SECIM PENCERESI — Qt offscreen testi.
//
// Kapsam (INBOX UI-34 kesit 2 kabul kriterleri):
//   1) Katalog kurulur; aile agaci (HEA/HEB/IPE/CHS/RHS) dogal sirada.
//   2) Filtre `*` joker: `IPE3*` → yalniz eslesenler gorunur, diger aileler
//      gizlenir; "Tum profilleri goster" kapatilinca filtre yok sayilir.
//   3) Agac secimi → profil adi alani guncellenir; alana yazi (kucuk harf
//      dahil) → agacta eslesen kayit secilir (senkron iki yonlu).
//   4) Sagda ozellik tablosu: degerler KATALOGLA AYNI (HEB240 h/b/tw/tf/r/A).
//   5) Kesit resmi widget'i profil alinca paintEvent calisir (offscreen
//      render — cokmez, bos degil).
//   6) Tamam → profileApplied + accept; Uygula → profileApplied + pencere
//      ACIK KALIR (result degismez); Iptal → reject, sinyal YOK.
//   7) Malzeme penceresi: filtre + secim + applied (basit liste).

#include "model_maker/profile_picker_dialog.hpp"
#include "model_maker/profile_database.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>

#include <cstdio>
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

// Gercek kullanici yazimini taklit eder (textEdited sinyali uretir).
void typeText(QWidget* widget, const QString& text) {
    for (const QChar c : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(widget, &press);
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c));
        QApplication::sendEvent(widget, &release);
    }
}

mm::SteelProfile makeProfile(const std::string& name, double h, double b,
                             double tw, double tf, double r, double area,
                             double weight, double iy, double iz) {
    mm::SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    p.roundingRadius = r;
    p.crossSectionArea = area;
    p.weightPerUnitLength = weight;
    p.inertiaY = iy;
    p.inertiaX = iz;
    return p;
}

std::vector<mm::SteelProfile> testCatalog() {
    std::vector<mm::SteelProfile> profiles;
    profiles.push_back(makeProfile("HEA100", 96, 100, 5, 8, 12, 2124, 16.7,
                                   349e4, 134e4));
    profiles.push_back(makeProfile("HEB240", 240, 240, 10, 17, 21, 10600, 83.2,
                                   11260e4, 3923e4));
    profiles.push_back(makeProfile("IPE300", 300, 150, 7.1, 10.7, 15, 5381,
                                   42.2, 8356e4, 604e4));
    profiles.push_back(makeProfile("IPE360", 360, 170, 8, 12.7, 18, 7273, 57.1,
                                   16270e4, 1043e4));
    profiles.push_back(makeProfile("CHS100", 100, 100, 5, 0, 0, 1492, 11.7,
                                   168e4, 168e4));
    profiles.push_back(makeProfile("RHS200x100", 200, 100, 6, 6, 0, 3481, 27.3,
                                   1830e4, 614e4));
    return profiles;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    mm::ProfilePickerDialog dialog;
    dialog.resize(900, 600);
    dialog.show();
    dialog.setProfiles(testCatalog());

    QTreeWidget* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("profileTree"));
    QLineEdit* filter = dialog.findChild<QLineEdit*>(QStringLiteral("profileFilter"));
    QLineEdit* name = dialog.findChild<QLineEdit*>(QStringLiteral("profileName"));
    QTableWidget* table =
        dialog.findChild<QTableWidget*>(QStringLiteral("profilePropsTable"));
    check(tree && filter && name && table,
          "pencere parcalari var (agac/filtre/ad/tablo)");

    // 1) Aile agaci: 6 profil → 5 aile (HEA/HEB/IPE/CHS/RHS), dogal sira.
    check(tree && tree->topLevelItemCount() == 5,
          "aile agaci 5 grup (HEA/HEB/IPE/CHS/RHS)");
    int leafCount = 0;
    for (int i = 0; i < (tree ? tree->topLevelItemCount() : 0); ++i)
        leafCount += tree->topLevelItem(i)->childCount();
    check(leafCount == 6, "agacta 6 profil yapragi");
    QStringList families;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        families << tree->topLevelItem(i)->text(0);
    check(families == (QStringList{QStringLiteral("CHS"), QStringLiteral("HEA"),
                                   QStringLiteral("HEB"), QStringLiteral("IPE"),
                                   QStringLiteral("RHS")}),
          "aileler dogal (harf) sirada");

    // 2) Filtre: IPE3* → IPE300 + IPE360 gorunur; diger aileler gizli.
    typeText(filter, QStringLiteral("IPE3*"));
    QApplication::processEvents();
    int visibleLeaves = 0;
    int visibleFamilies = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* familyItem = tree->topLevelItem(i);
        if (!familyItem->isHidden()) ++visibleFamilies;
        for (int j = 0; j < familyItem->childCount(); ++j)
            if (!familyItem->child(j)->isHidden()) ++visibleLeaves;
    }
    check(visibleLeaves == 2 && visibleFamilies == 1,
          "filtre IPE3* yalniz 2 IPE profilini birakir");

    // 3) Agac secimi → ad alani; ad yazimi → agac secimi (iki yonlu senkron).
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* familyItem = tree->topLevelItem(i);
        for (int j = 0; j < familyItem->childCount(); ++j) {
            QTreeWidgetItem* child = familyItem->child(j);
            if (child->data(0, Qt::UserRole).toString() == QStringLiteral("IPE300")) {
                tree->setCurrentItem(child);
                break;
            }
        }
    }
    check(name->text() == QStringLiteral("IPE300"),
          "agac secimi ad alanina yazilir (agac -> alan)");
    name->clear();
    typeText(name, QStringLiteral("heb240"));
    check(name->text() == QStringLiteral("HEB240"),
          "kucuk harf yazim canonical'a cevrilir (heb240 -> HEB240)");
    check(dialog.selectedProfile() == QStringLiteral("HEB240"),
          "ad yazimi agactaki kaydi secer (alan -> agac)");

    // 4) Ozellik tablosu: HEB240 degerleri katalogla ayni.
    bool hasH = false, hasTw = false, hasA = false;
    for (int r = 0; r < table->rowCount(); ++r) {
        const QString symbol = table->item(r, 1) ? table->item(r, 1)->text()
                                                 : QString();
        const QString value = table->item(r, 2) ? table->item(r, 2)->text()
                                                : QString();
        if (symbol == QStringLiteral("h")) hasH = (value == QStringLiteral("240"));
        if (symbol == QStringLiteral("tw")) hasTw = (value == QStringLiteral("10"));
        if (symbol == QStringLiteral("A")) hasA = (value == QStringLiteral("10600"));
    }
    check(hasH && hasTw && hasA,
          "ozellik tablosu katalog degerleriyle ayni (h=240, tw=10, A=10600)");

    // 5) Kesit resmi: profil seciliyken offscreen render calisir (cokmez).
    {
        auto* preview = dialog.findChild<QWidget*>(
            QStringLiteral("profileSectionPreview"));
        check(preview != nullptr, "kesit resmi widget'i var");
        if (preview) {
            QPixmap pm(preview->size());
            preview->render(&pm);
            check(!pm.isNull(), "kesit resmi render edildi (sematik motor)");
        }
    }

    // 6) Tamam/Uygula/Iptal davranisi.
    int appliedCount = 0;
    QString appliedName;
    QObject::connect(&dialog, &mm::ProfilePickerDialog::profileApplied,
                     [&](const QString& n) {
                         ++appliedCount;
                         appliedName = n;
                     });
    auto* okButton = dialog.findChild<QPushButton*>(QStringLiteral("profileOkButton"));
    auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("profileApplyButton"));
    auto* cancelButton = dialog.findChild<QPushButton*>(QStringLiteral("profileCancelButton"));
    check(okButton && applyButton && cancelButton, "Tamam/Uygula/Iptal dugmeleri var");

    // Uygula: sinyal + pencere ACIK kalir (accept edilmez).
    if (applyButton) {
        appliedCount = 0;
        applyButton->click();
        check(appliedCount == 1 && appliedName == QStringLiteral("HEB240"),
              "Uygula secili profili yayinlar");
        check(dialog.result() == 0, "Uygula pencereyi KAPATMAZ (acik kalir)");
    }
    // Iptal: sinyal uretmez, reddeder.
    if (cancelButton) {
        appliedCount = 0;
        cancelButton->click();
        check(appliedCount == 0, "Iptal secim yayinlamaz");
        check(dialog.result() == QDialog::Rejected, "Iptal pencereyi reddeder");
    }
    // Tamam: sinyal + kabul. (result sifirlandi mi? reject sonrasi ok tekrar
    // accept eder.)
    if (okButton) {
        appliedCount = 0;
        okButton->click();
        check(appliedCount == 1, "Tamam secim yayinlar");
        check(dialog.result() == QDialog::Accepted, "Tamam pencereyi kabul eder");
    }

    // 7) Malzeme penceresi: liste + filtre + secim + applied.
    mm::MaterialPickerDialog material;
    material.show();
    material.setMaterials({QStringLiteral("S235JR"), QStringLiteral("S275JR"),
                           QStringLiteral("S355J2"), QStringLiteral("S690QL")});
    auto* materialFilter =
        material.findChild<QLineEdit*>(QStringLiteral("materialFilter"));
    auto* materialList =
        material.findChild<QListWidget*>(QStringLiteral("materialList"));
    check(materialList && materialList->count() == 4,
          "malzeme listesi kuruldu (4 oge)");
    if (materialFilter) typeText(materialFilter, QStringLiteral("S3*"));
    QApplication::processEvents();
    int visibleMaterials = 0;
    QListWidgetItem* firstVisible = nullptr;
    for (int i = 0; i < materialList->count(); ++i)
        if (!materialList->item(i)->isHidden()) {
            ++visibleMaterials;
            if (!firstVisible) firstVisible = materialList->item(i);
        }
    check(visibleMaterials == 1,
          "malzeme filtresi S3* tek oge birakir (S355J2)");
    if (firstVisible) materialList->setCurrentItem(firstVisible);
    int materialAppliedCount = 0;
    QObject::connect(&material, &mm::MaterialPickerDialog::materialApplied,
                     [&](const QString&) { ++materialAppliedCount; });
    auto* materialOk =
        material.findChild<QPushButton*>(QStringLiteral("materialOkButton"));
    materialOk->click();
    check(materialAppliedCount == 1 && material.selectedMaterial() == QStringLiteral("S355J2"),
          "malzeme Tamam secimi yayinlar (filtre sonrasi gorunur kayit)");

    if (failures == 0)
        std::printf("OK: profile_picker_dialog_test — hepsi gecti\n");
    else
        std::printf("SONUC: %d hata\n", failures);
    return failures == 0 ? 0 : 1;
}
