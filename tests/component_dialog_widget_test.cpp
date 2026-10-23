// UNIVERSAL COMPONENT DIALOG — Qt offscreen testi (Linux, Qt6 varken).
//
// Widget'ın gerçek davranışını başsız (offscreen) doğrular: end plate şeması
// kurulur, düzenlenmemiş değerler round-trip'te korunur (özellikle -1e9
// "tanımsız" sentinel'leri — spinbox kelepçelememeli), bilinmeyen (ekstra)
// parametreler korunur, fin plate şeması kurulur ve şema doğrulaması geçer.
// Windows'ta bu test kurulmaz (model-maker exe'si görsel olarak aynı widget'ı
// kullanır); Linux'ta Qt6 yoksa CMake bu testi hiç eklemez.

#include "model_maker/component_dialog_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            ++failures;                                           \
            std::printf("FAIL: %s\n", msg);                       \
        }                                                         \
    } while (0)

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 1) End plate şeması kurulur + round-trip sentinel'leri korur.
    const mm::ComponentDialogDefinition ep = mm::endPlateDialogDefinition();
    CHECK(ep.schema.typeId == L"beam_to_column_end_plate", "end plate typeId");
    CHECK(ep.specs.size() == 27, "end plate 27 alan");
    CHECK(ep.schema.groups.size() == 2, "end plate 2 grup");

    mm::EndPlateParams p0;
    const auto current = mm::endPlateToParams(p0);
    mm::ComponentDialogWidget w(ep, current);
    w.resize(900, 640);
    w.show();

    const auto out = w.resultParams();
    const mm::EndPlateParams p1 = mm::endPlateFromParams(out);
    CHECK(p1 == p0, "end plate round-trip korunmali");
    // Sentinel'ler açıkça: spinbox clamp'i -1e9'u ezmemeli.
    CHECK(p1.upperEdgeDistance == -1e9, "upperEdgeDistance sentinel -1e9 korunmali");
    CHECK(p1.lowerEdgeDistance == -1e9, "lowerEdgeDistance sentinel -1e9 korunmali");
    CHECK(p1.topExtra == -1.0 && p1.bottomExtra == -1.0,
          "top/bottomExtra legacy sentineli korunmali");
    CHECK(p1.boltOffsetV == -1.0, "boltOffsetV sentineli korunmali");

    // 2) Bilinmeyen (ekstra) parametre korunur.
    std::vector<mm::CompParam> extra = current;
    extra.push_back(mm::CompParam{L"future_field", 42.0});
    mm::ComponentDialogWidget w2(ep, extra);
    const auto out2 = w2.resultParams();
    bool preserved = false;
    for (const auto& p : out2)
        if (p.name == L"future_field") preserved = true;
    CHECK(preserved, "bilinmeyen parametre korunmali");

    // 3) Fin plate şeması kurulur (engine specs'ten türer).
    const mm::ComponentDialogDefinition fp = mm::finPlateDialogDefinition();
    CHECK(fp.typeId == L"fin_plate", "fin plate typeId");
    CHECK(!fp.specs.empty(), "fin plate specs bos degil");
    CHECK(fp.schema.groups.size() >= 2, "fin plate gruplari");
    CHECK(fp.schema.hotspots.size() >= 4, "fin plate hotspotlari");

    // 4) Şema doğrulaması (portable core üzerinden).
    const auto v = mm::validateDialogSchema(ep.schema, ep.specs);
    CHECK(v.ok, "end plate sema dogrulamasi");
    const auto vf = mm::validateDialogSchema(fp.schema, fp.specs);
    CHECK(vf.ok, "fin plate sema dogrulamasi");

    // 5) Hit-test: hotspot dışı boş alan nullopt, hotspot içi id döner.
    const auto miss = mm::hitTestDiagramHotspot(ep.schema, mm::DiagramPoint{300.0, 10.0});
    CHECK(!miss.has_value(), "bos alan hotspot dondurmemeli");

    // 6) Tekla 146 diyalogu: SEKMELER gercekten kurulur ve "Default" onay
    //    kutusu isaretsiz birakilinca parametre sonuca YAZILMAZ.
    {
        const mm::ComponentDialogDefinition d = mm::shearPlate146DialogDefinition();
        CHECK(d.schema.useTabs, "146: sekmeli duzen");
        const std::vector<mm::CompParam> empty;
        mm::ComponentDialogWidget w146(d, empty);
        w146.resize(1000, 700);
        w146.show();

        auto* tabWidget = w146.findChild<QTabWidget*>();
        CHECK(tabWidget != nullptr, "146: QTabWidget kuruldu");
        // 13 sekme, Tekla 146 sirasi; "Welds" sekme DEGIL, ust cubuktaki dugme.
        CHECK(tabWidget && tabWidget->count() == 13, "146: 13 sekme (Welds sekme degil)");
        if (tabWidget) {
            const char* want[] = {"Picture", "Plates", "Stiffeners", "General", "Haunch",
                                  "Notch", "Bolts", "Design type", "Beam cut", "Angle box",
                                  "BoxPBolts", "BoxSBolts", "Analysis"};
            for (int i = 0; i < 13 && i < tabWidget->count(); ++i)
                CHECK(tabWidget->tabText(i) == QString::fromLatin1(want[i]),
                      "146: sekme sirasi Tekla ile ayni");
            for (int i = 0; i < tabWidget->count(); ++i)
                CHECK(tabWidget->tabText(i) != QStringLiteral("Welds"),
                      "146: Welds sekme olarak yok");
        }
        // Ust cubuk: Welds onay kutusu + Welds... dugmesi + Help... + gri Save/Load.
        {
            bool weldsBtn = false, helpBtn = false, saveGrey = false;
            for (auto* b : w146.findChildren<QPushButton*>()) {
                if (b->text() == QStringLiteral("Welds...")) weldsBtn = b->isEnabled();
                if (b->text() == QStringLiteral("Help...")) helpBtn = b->isEnabled();
                if (b->text() == QStringLiteral("Save")) saveGrey = !b->isEnabled();
            }
            CHECK(weldsBtn, "146: Welds... dugmesi var ve acik");
            CHECK(helpBtn, "146: Help... dugmesi 146 icin acik");
            CHECK(saveGrey, "146: Save dugmesi gri (karsiligi yok)");
        }
        // Parca satirlari: Plates sekmesinde Tab plate t/b/h kutulari + Default onay kutusu.
        {
            int groupBoxLabels = 0;
            for (auto* l : w146.findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Tab plate") ||
                    l->text() == QStringLiteral("Plate washer") ||
                    l->text() == QStringLiteral("Web stiffener") ||
                    l->text() == QStringLiteral("Top angle") ||
                    l->text() == QStringLiteral("Bottom angle") ||
                    l->text() == QStringLiteral("Backing bar"))
                    ++groupBoxLabels;
            CHECK(groupBoxLabels >= 6, "146: Tekla parca satiri etiketleri cizildi");
        }
        // Alt cubuk: OK/Apply/Modify/Get/toggle/Cancel/Create; Apply gri.
        {
            QDialog dlgProbe;
            QWidget* bar = w146.createButtonBar(&dlgProbe);
            int total = 0;
            bool applyGrey = false, okOn = false, cancelOn = false, createGrey = false;
            for (auto* b : bar->findChildren<QPushButton*>()) {
                ++total;
                if (b->text() == QStringLiteral("Apply")) applyGrey = !b->isEnabled();
                if (b->text() == QStringLiteral("OK")) okOn = b->isEnabled();
                if (b->text() == QStringLiteral("Cancel")) cancelOn = b->isEnabled();
                if (b->text() == QStringLiteral("Create")) createGrey = !b->isEnabled();
            }
            CHECK(total == 7, "146: alt cubukta 7 dugme");
            CHECK(applyGrey && createGrey, "146: Apply/Create gri");
            CHECK(okOn && cancelOn, "146: OK/Cancel acik");
        }

        // Baslangicta Default kutulari isaretli -> alan sonuca yazilir.
        auto params = w146.resultParams();
        bool hasStandard = false;
        for (const auto& p : params)
            if (p.name == L"boltStandard") hasStandard = true;
        CHECK(hasStandard, "146: boltStandard sonuca yazilir (kutu isaretli)");

        // Tekla yardim sayfasi sekilleri gomulu kaynaktan yuklenir (sekme basina).
        {
            int pixLabels = 0;
            for (auto* l : w146.findChildren<QLabel*>())
                if (!l->pixmap(Qt::ReturnByValue).isNull()) ++pixLabels;
            CHECK(pixLabels >= 22, "146: Tekla sekilleri yuklendi (>=22 resim)");
        }

        // Gri (documentedOnly) alanlar sonuca HIC yazilmaz; calisan alanlar yazilir.
        {
            bool docWritten = false, plateWritten = false;
            for (const auto& p : params) {
                if (p.name == L"haunchCreation" || p.name == L"notchShape" ||
                    p.name == L"tabOrientation" || p.name == L"flangeStripSize")
                    docWritten = true;
                if (p.name == L"plateThickness") plateWritten = true;
            }
            CHECK(!docWritten, "146: gri alanlar sonuca yazilmaz");
            CHECK(plateWritten, "146: calisan alan sonuca yazilir");
            int disabledEditors = 0;
            for (auto* cb : w146.findChildren<QComboBox*>())
                if (!cb->isEnabled()) ++disabledEditors;
            CHECK(disabledEditors > 0, "146: gri alan editorleri devre disi");
        }

        // Tum ACIK "Default" onay kutularini (tooltip ile) kaldir: bulon alani VE
        // parca satirindaki (Tab plate t/b/h) alan sonuca yazilmamali. Gri
        // alanlarin kutulari zaten kapali/devre disi.
        int optCount = 0, optEnabled = 0;
        for (auto* cb : w146.findChildren<QCheckBox*>()) {
            if (!cb->toolTip().contains(QStringLiteral("Default"))) continue;
            ++optCount;
            if (cb->isEnabled()) { ++optEnabled; cb->setChecked(false); }
        }
        CHECK(optCount > 0, "146: Default onay kutusu bulundu");
        CHECK(optEnabled > 0 && optEnabled < optCount, "146: gri alanlarin Default kutusu devre disi");
        if (optCount > 0) {
            params = w146.resultParams();
            bool stillThere = false, plateThere = false;
            for (const auto& p : params) {
                if (p.name == L"boltStandard") stillThere = true;
                if (p.name == L"plateThickness") plateThere = true;
            }
            CHECK(!stillThere, "146: Default isaretsiz -> parametre yazilmaz");
            CHECK(!plateThere, "146: parca satiri Default isaretsiz -> t yazilmaz");
        }

        // 7) KAYNAK TABLOSU (Tekla "Welds"): 10 satir x 12 sutun, cagri eslesmesi.
        auto* table = w146.findChild<QTableWidget*>();
        CHECK(table != nullptr, "146: kaynak tablosu kuruldu");
        if (table) {
            CHECK(table->rowCount() == 10, "146: 10 kaynak satiri");
            CHECK(table->columnCount() == 12, "146: 12 kaynak sutunu");
            CHECK(table->horizontalHeaderItem(0)->text() == QStringLiteral("No"),
                  "146: ilk sutun No");
            CHECK(table->horizontalHeaderItem(6)->text() == QStringLiteral("Eff.thr."),
                  "146: Eff.thr. sutunu");
            CHECK(table->horizontalHeaderItem(8)->text() == QStringLiteral("Placement"),
                  "146: Placement sutunu");
            CHECK(table->horizontalHeaderItem(11)->text() == QStringLiteral("Pitch"),
                  "146: Pitch sutunu");

            auto rows = w146.resultWeldRows();
            CHECK(rows.size() == 10, "146: sonuc 10 kaynak satiri");
            bool allEnabled = rows.size() == 10;
            for (const auto& r : rows) allEnabled = allEnabled && r.enabled;
            CHECK(allEnabled, "146: kaynak satirlari varsayilan acik");
            CHECK(!rows.empty() && rows[0].size == 5.0 && rows[0].effThroat == 3.5,
                  "146: varsayilan a=5 / bogaz 3.5");

            // No sutunundaki onay kutusunu kaldir -> satir devre disi.
            if (auto* no = table->item(0, 0)) {
                no->setCheckState(Qt::Unchecked);
                rows = w146.resultWeldRows();
                CHECK(!rows.empty() && !rows[0].enabled, "146: onay kutusu -> satir kapali");
            }

            // Tablo satiri secilince semadaki ayni numarali cagri vurgulanir.
            table->selectRow(3);
            auto* sch = w146.findChild<mm::ComponentSchematicView*>();
            CHECK(sch != nullptr, "146: sematik gorunum bulundu");
            CHECK(sch && sch->highlightedWeldRow() == 3,
                  "146: satir secimi -> semada 4 numarali cagri vurgulanir");
        }
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("component_dialog_widget: TUM KONTROLLER GECTI\n");
    return 0;
}
