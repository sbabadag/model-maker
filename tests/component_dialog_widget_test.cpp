// UNIVERSAL COMPONENT DIALOG — Qt offscreen testi (Linux/Windows, Qt6 varken).
//
// Widget'ın gerçek davranışını başsız (offscreen) doğrular: end plate şeması
// kurulur, düzenlenmemiş değerler round-trip'te korunur (özellikle -1e9
// "tanımsız" sentinel'leri — spinbox kelepçelememeli), bilinmeyen (ekstra)
// parametreler korunur, fin plate şeması kurulur ve şema doğrulaması geçer.
// Windows ve Linux'ta Qt6 mevcutsa CMake bu testi ekler.

#include "model_maker/component_dialog_widget.hpp"
#include "model_maker/component_live_preview_widget.hpp"
#include "model_maker/component_engine.hpp"
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QImage>
#include <QStyle>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>

#include <cstdio>
#include <cstdlib>

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
        // 13 Tekla sekmesi; Tekla'da karsiligi olmayan motor alanlari (delik capi,
        // sira araligi, bulon sinifi) icin en sonda "Diğer" sekmesi.
        CHECK(tabWidget && tabWidget->count() == 14 &&
                  tabWidget->tabText(13) == QStringLiteral("Diğer"),
              "146: 13 Tekla sekmesi + Diğer (Welds sekme degil)");
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
        // TEKLA SEKMELERI: her sekmede Tekla ekran goruntusu arka plan; kontroller
        // goruntudeki beyaz kutunun TAM ustunde; sol sematik gizli.
        {
            int backgrounds = 0;
            for (auto* l : w146.findChildren<QLabel*>())
                if (l->objectName() == QStringLiteral("teklaCanvasBackground") &&
                    !l->pixmap(Qt::ReturnByValue).isNull())
                    ++backgrounds;
            CHECK(backgrounds == 13, "146: 13 Tekla sekme resmi yuklendi");
            bool geomOk = true;
            int placedEditors = 0;
            for (const auto& g : d.schema.groups)
                for (const auto& c : g.placed) {
                    if (c.kind == mm::PlacedControlKind::DefaultCheck) continue;
                    auto* ed = w146.findChild<QWidget*>(QString::fromStdWString(c.param));
                    if (!ed) { geomOk = false; continue; }
                    ++placedEditors;
                    const QRect r = ed->geometry();
                    // Kutu (x,y,w,h) editorun icinde kalmali, en fazla 3 px tasma.
                    if (!(r.left() <= c.x && r.top() <= c.y && r.right() >= c.x + c.w - 1 &&
                          r.bottom() >= c.y + c.h - 1 && c.x - r.left() <= 3 && c.y - r.top() <= 3))
                        geomOk = false;
                }
            CHECK(placedEditors > 250, "146: Tekla sekmelerinde editorler kuruldu");
            CHECK(geomOk, "146: her editor Tekla kutusunun ustunde (piksel)");
            auto* plateT = w146.findChild<QWidget*>(QStringLiteral("plateThickness"));
            CHECK(plateT && plateT->isEnabled() && plateT->geometry() == QRect(124, 29, 40, 20),
                  "146: Plates t kutusu Tekla konumunda ve aktif");
            // Bolt size Tekla'da acilir liste: sayisal deger listeden okunur/yazilir.
            auto* boltSize = w146.findChild<QComboBox*>(QStringLiteral("boltDiameter"));
            CHECK(boltSize && boltSize->isEditable() && boltSize->currentText() == QStringLiteral("20"),
                  "146: Bolt size acilir liste, varsayilan 20");
            if (boltSize) {
                boltSize->setCurrentText(QStringLiteral("24"));
                bool got24 = false;
                for (const auto& p : w146.resultParams())
                    if (p.name == L"boltDiameter")
                        if (const auto* dv = std::get_if<double>(&p.value)) got24 = *dv == 24.0;
                CHECK(got24, "146: Bolt size 24 -> boltDiameter=24");
                boltSize->setCurrentText(QStringLiteral("20"));
            }
            auto* sch = w146.findChild<mm::ComponentSchematicView*>();
            CHECK(sch && sch->isHidden(), "146: sol sematik gizli (Tekla'da yok)");
            // Gorsel karsilastirma icin istege bagli: her Tekla sekmesini PNG'ye yaz.
            if (const char* dir = std::getenv("MM_DIALOG_SNAPSHOT_DIR")) {
                for (auto* l : w146.findChildren<QLabel*>()) {
                    if (l->objectName() != QStringLiteral("teklaCanvasBackground")) continue;
                    QWidget* page = l->parentWidget();
                    for (int i = 0; i < tabWidget->count(); ++i)
                        if (tabWidget->widget(i)->isAncestorOf(page)) {
                            tabWidget->setCurrentIndex(i);
                            QApplication::processEvents();
                            page->grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/") +
                                              tabWidget->tabText(i) + QStringLiteral(".png"));
                        }
                }
            }
        }
        // Stiffeners sekmesi: Tekla ekraninda gri cizilen "gap to flange" alani artik
        // GERCEK motor parametresi -> aktif; ayrica tum Stiffeners alanlari aktif.
        {
            int found = 0, enabled = 0;
            for (const char* nm : {"stiffenerCreation", "stiffenerOrientation", "stiffenerT", "stiffenerW",
                                   "stiffenerH", "stiffenerGapFlange", "stiffenerGapWeb",
                                   "stiffenerChamferType", "stiffenerChamferH", "stiffenerChamferV",
                                   "stiffenerMaterial", "stiffenerName", "stiffenerFinish"}) {
                QWidget* e = w146.findChild<QWidget*>(QString::fromLatin1(nm));
                if (!e) continue;
                ++found;
                if (e->isEnabled()) ++enabled;
            }
            CHECK(found == 13 && enabled == 13, "146: Stiffeners alanlari (13) tamami aktif");
        }
        // Acilir listeler: Tekla yardim sayfasi secenek resimleri her satirda.
        {
            int iconCombos = 0, allIcon = 0;
            for (const char* nm : {"stiffenerCreation", "stiffenerOrientation", "stiffenerChamferType",
                                   "tabChamferType", "tabOrientation", "boltStagger", "notchShape",
                                   "haunchCreation", "shearTabPosition", "plateWasherType"}) {
                auto* cb = w146.findChild<QComboBox*>(QString::fromLatin1(nm));
                if (!cb) continue;
                ++iconCombos;
                bool all = cb->count() > 0;
                for (int i = 0; i < cb->count(); ++i)
                    if (cb->itemIcon(i).isNull() || cb->itemIcon(i).pixmap(60, 40).isNull()) all = false;
                if (all) ++allIcon;
                else std::printf("  resimsiz satir: %s\n", nm);
            }
            CHECK(iconCombos == 10 && allIcon == 10, "146: acilir listelerin her satirinda secenek resmi");
        }
        // Acilir liste KAYIK acilmaz: popup combo'nun ALTINDA, sol kenarlari hizali.
        {
            auto* cb = w146.findChild<QComboBox*>(QStringLiteral("stiffenerCreation"));
            bool ok = (cb != nullptr);
            if (cb) {
                w146.show();
                QApplication::processEvents();
                cb->showPopup();
                QApplication::processEvents();
                QWidget* pop = cb->view()->window();
                const QPoint belowLeft = cb->mapToGlobal(QPoint(0, cb->height()));
                const QRect g = pop->geometry();
                std::printf("  popup (%d,%d %dx%d) combo-alti (%d,%d)\n", g.x(), g.y(), g.width(), g.height(),
                            belowLeft.x(), belowLeft.y());
                // Ekrana siginca altta; sigmazsa ustte (alt kenar = combo ustu): ikisi de "hizali".
                const QPoint topLeft = cb->mapToGlobal(QPoint(0, 0));
                const bool below = std::abs(g.x() - belowLeft.x()) <= 2 && std::abs(g.y() - belowLeft.y()) <= 2;
                const bool above = std::abs(g.x() - topLeft.x()) <= 2 && std::abs(g.bottom() + 1 - topLeft.y()) <= 2;
                ok = (below || above) && g.width() >= cb->width();
                cb->hidePopup();
            }
            CHECK(ok, "146: acilir liste combo'ya hizali acilir (kayik degil)");
        }
        // Secim degisince yanina AYRI yazi/edit kutusu cikmaz; secilen secenegin resmi
        // combo alaninda gosterilir.
        {
            auto* cb = w146.findChild<QComboBox*>(QStringLiteral("stiffenerCreation"));
            bool ok = (cb != nullptr);
            if (cb) {
                const int before = w146.findChildren<QLabel*>().size();
                const int cur = cb->currentIndex();
                cb->setCurrentIndex((cur + 1) % cb->count());
                QApplication::processEvents();
                int withPix = 0, withText = 0;
                for (auto* l : w146.findChildren<QLabel*>())
                    if (!l->isHidden() && l->geometry() == cb->geometry()) {
                        if (!l->pixmap(Qt::ReturnByValue).isNull()) ++withPix;
                        else if (!l->text().isEmpty()) ++withText;
                    }
                ok = withPix == 1 && withText == 0 && w146.findChildren<QLabel*>().size() == before;
                cb->setCurrentIndex(cur);
            }
            CHECK(ok, "146: secim degisince combo alaninda secenegin RESMI gosterilir, ek yazi kutusu yok");
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
        // Eylem atanirsa Apply/Modify/Get/Create ACIK ve geri cagriyi cagirir;
        // Get -> setParams ile pencere degerleri degisir (modeless Tekla akisi).
        {
            QDialog dlgProbe;
            int nApply = 0, nModify = 0, nGet = 0, nCreate = 0;
            mm::ComponentDialogWidget::ButtonActions act;
            act.apply = [&]() { ++nApply; };
            act.modify = [&]() { ++nModify; };
            act.get = [&]() { ++nGet; };
            act.create = [&]() { ++nCreate; };
            QWidget* bar = w146.createButtonBar(&dlgProbe, act);
            bool allOn = true;
            for (auto* b : bar->findChildren<QPushButton*>()) {
                const QString t = b->text();
                if (t == QStringLiteral("Apply") || t == QStringLiteral("Modify") ||
                    t == QStringLiteral("Get") || t == QStringLiteral("Create")) {
                    if (!b->isEnabled()) allOn = false;
                    b->click();
                }
            }
            CHECK(allOn, "146: eylemli cubukta Apply/Modify/Get/Create acik");
            CHECK(nApply == 1 && nModify == 1 && nGet == 1 && nCreate == 1,
                  "146: dugmeler geri cagrilari tetikler");

            std::vector<mm::CompParam> src = w146.resultParams();
            for (auto& p : src)
                if (p.name == L"plateThickness") p.value = mm::CompParamValue{18.0};
                else if (p.name == L"holeType") p.value = mm::CompParamValue{std::wstring(L"Slotted")};
                else if (p.name == L"boltCount") p.value = mm::CompParamValue{6};
            w146.setParams(src);
            double t = 0.0; int n = 0; std::wstring ht;
            for (const auto& p : w146.resultParams()) {
                if (p.name == L"plateThickness") t = std::get<double>(p.value);
                if (p.name == L"boltCount") n = std::get<int>(p.value);
                if (p.name == L"holeType") ht = std::get<std::wstring>(p.value);
            }
            CHECK(t == 18.0 && n == 6 && ht == L"Slotted",
                  "146: setParams (Get) pencere degerlerini yukler");
        }

        // Baslangicta Default kutulari isaretli -> alan sonuca yazilir.
        auto params = w146.resultParams();
        bool hasStandard = false;
        for (const auto& p : params)
            if (p.name == L"boltStandard") hasStandard = true;
        CHECK(hasStandard, "146: boltStandard sonuca yazilir (kutu isaretli)");

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

    // Native Haunch 40 P1/P2/P3: real generator's 3D plan, 3 orthographic
    // mini-previews and one synchronized model-overlay dimension edit.
    {
        mm::registerBuiltInComponents(mm::ComponentRegistry::instance());
        mm::ComponentDialogWidget h40(mm::haunch40DialogDefinition(),{});
        h40.resize(1140,760);
        mm::ComponentInput input;
        input.main.id=mm::EntityId{711};
        input.main.axisFrom={0,0,0};
        input.main.axisTo={0,0,3000};
        input.main.refNormal={0,1,0};
        input.main.secWidth=300;input.main.secHeight=300;
        input.main.secWebThickness=11;input.main.secFlangeThickness=19;
        mm::MemberGeometry beam;
        beam.id=mm::EntityId{712};
        beam.axisFrom={0,1200,1500};
        beam.axisTo={0,60,1500};
        beam.refNormal={0,0,1};
        beam.secWidth=100;beam.secHeight=200;
        beam.secWebThickness=5.6;beam.secFlangeThickness=8.5;
        input.secondary.push_back(beam);
        h40.setPreviewInput(input);
        h40.show();
        QApplication::processEvents();
        const auto previews=h40.findChildren<mm::ComponentLivePreviewWidget*>();
        CHECK(previews.size()==4,"Haunch 40: main 3D + 3 live orthographic previews");
        auto* left = previews.empty()?nullptr:previews.first();
        CHECK(left!=nullptr,"Haunch 40: real 3D preview widget exists");
        if(left) {
            CHECK(left->activeRole().empty() &&
                  left->visibleOverlayParams().empty(),
                  "P4: no component selected -> no floating edit boxes");
            left->selectRole("haunch_web");
            QApplication::processEvents();
            const auto webFields=left->visibleOverlayParams();
            CHECK(webFields.size()==4 &&
                  std::find(webFields.begin(),webFields.end(),L"haunchLength")!=webFields.end() &&
                  std::find(webFields.begin(),webFields.end(),L"haunchDepth")!=webFields.end(),
                  "P4: only selected web's four parameters appear");
            // Actual left-button events at the cap's projected generator
            // anchor must pick that role (not only direct selectRole calls).
            const QPointF capClick=left->roleScreenAnchor("haunch_top_plate");
            QMouseEvent capDown(QEvent::MouseButtonPress,capClick,
                                Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent capUp(QEvent::MouseButtonRelease,capClick,
                              Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(left,&capDown);
            QApplication::sendEvent(left,&capUp);
            QApplication::processEvents();
            CHECK(left->activeRole()=="haunch_top_plate",
                  "P4: real mouse click picks projected cap-plate polygon");
            left->selectRole("haunch_top_plate");
            QApplication::processEvents();
            const auto capFields=left->visibleOverlayParams();
            CHECK(capFields.size()==1 && capFields.front()==L"topPlateThickness",
                  "P4: column cap shows only its own thickness");
            left->selectRole("haunch_bolts_lower");
            QApplication::processEvents();
            const auto lower=left->visibleOverlayParams();
            CHECK(lower.size()==7 &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltCountX")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltCountY")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltPitchX")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltPitchY")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltDiameter")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerHoleDiameter")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"lowerBoltEdgeClear")!=lower.end() &&
                  std::find(lower.begin(),lower.end(),L"upperBoltEdgeClear")==lower.end(),
                  "P4G: lower selects own six group parameters and ONLY bottom net edge");
            const auto low=left->boltOverlayMetrics("haunch_bolts_lower");
            const auto upperMetrics=left->boltOverlayMetrics("haunch_bolts_upper");
            CHECK(low.valid && upperMetrics.valid && low.count==4 &&
                  upperMetrics.count==4 && low.totalCount==8 &&
                  std::fabs(low.bottomEdge-55.0)<1e-6 &&
                  std::fabs(upperMetrics.topEdge-55.0)<1e-6,
                  "P4G: migrated defaults preserve physical bolt position");
            left->selectRole("haunch_bolts_upper");
            QApplication::processEvents();
            const auto upperFields=left->visibleOverlayParams();
            CHECK(upperFields.size()==7 &&
                  std::find(upperFields.begin(),upperFields.end(),
                            L"upperBoltEdgeClear")!=upperFields.end() &&
                  std::find(upperFields.begin(),upperFields.end(),
                            L"lowerBoltEdgeClear")==upperFields.end(),
                  "P4G: upper selection exposes ONLY upper net edge");
            QDoubleSpinBox* upperHole3d=nullptr;
            QDoubleSpinBox* upperCount3d=nullptr;
            for(auto* sp:left->findChildren<QDoubleSpinBox*>()) {
                if(sp->toolTip()==QStringLiteral("upperHoleDiameter"))upperHole3d=sp;
                if(sp->toolTip()==QStringLiteral("upperBoltCountX"))upperCount3d=sp;
            }
            auto* upperCount=h40.findChild<QSpinBox*>(
                QStringLiteral("upperBoltCountX"));
            auto* lowerCount=h40.findChild<QSpinBox*>(
                QStringLiteral("lowerBoltCountX"));
            CHECK(upperHole3d && upperCount3d && upperCount && lowerCount,
                  "P4G: independent 3D and right-panel count/hole controls");
            if(upperHole3d && upperCount3d && upperCount && lowerCount) {
                const auto oldLower=left->previewPlan().boltGroups[1];
                upperHole3d->setValue(24.0);
                QApplication::processEvents();
                const auto changed=left->boltOverlayMetrics("haunch_bolts_upper");
                CHECK(changed.valid &&
                      std::fabs(changed.holeDiameter-24.0)<1e-6 &&
                      std::fabs(left->boltOverlayMetrics("haunch_bolts_lower")
                                .holeDiameter-22.0)<1e-6,
                      "P4G: upper hole size edit leaves lower hole untouched");
                upperCount3d->setValue(3.0);
                QApplication::processEvents();
                const auto u=left->boltOverlayMetrics("haunch_bolts_upper");
                const auto l=left->boltOverlayMetrics("haunch_bolts_lower");
                CHECK(u.valid && l.valid && upperCount->value()==3 &&
                      lowerCount->value()==2 &&
                      u.count==6 && l.count==4 && u.totalCount==10,
                      "P4G: upper 3x2 becomes 6 while lower remains 2x2");
                upperHole3d->setValue(22.0);
                upperCount3d->setValue(2.0);
                QApplication::processEvents();
                CHECK(left->previewPlan().ok &&
                      oldLower.countX==left->previewPlan().boltGroups[1].countX,
                      "P4G: changing upper controls never mutates lower group layout");
            }
            // Exercise the real mouse path, not just the test selection API.
            // Top-left empty background must clear the active overlays.
            const QPointF empty(3.0,3.0);
            QMouseEvent down(QEvent::MouseButtonPress,empty,
                             Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease,empty,
                           Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(left,&down);
            QApplication::sendEvent(left,&up);
            QApplication::processEvents();
            CHECK(left->activeRole().empty() && left->visibleOverlayParams().empty(),
                  "P4: clicking empty viewport clears overlay without deleting context");
            left->selectRole("haunch_web");
            QApplication::processEvents();
        }
        QDoubleSpinBox* overlay=nullptr;
        if(left) {
            for(auto* d:left->findChildren<QDoubleSpinBox*>()) {
                if(d->toolTip()==QStringLiteral("haunchLength")) {
                    overlay=d;break;
                }
            }
        }
        CHECK(overlay!=nullptr,"Haunch 40: editable haunchLength overlay");
        if(overlay) {
            overlay->setValue(570.0);   // preview -> real component form
            QApplication::processEvents();
            // resultParams() returns a vector BY VALUE. Never take an
            // element pointer from that temporary: the previous test used
            // a dangling CompParam* and reported a spurious sync failure.
            const auto formParams=h40.resultParams();
            const mm::CompParam* p=mm::findParam(formParams,L"haunchLength");
            CHECK(p && std::holds_alternative<double>(p->value) &&
                  std::fabs(std::get<double>(p->value)-570.0)<1e-6,
                  "Haunch 40: preview edit synchronizes right-side real params");
            const auto& plan=left->previewPlan();
            CHECK(plan.ok && plan.plates.size()>=3 &&
                  std::fabs(plan.plates[1].width-570.0)<1e-6,
                  "P6: editing child length regenerates its native haunch web");
            CHECK(plan.cuts.size()==1 && plan.boltGroups.size()==2,
                  "P6: dependent parent cut and both bolt groups remain attached");
        }
        if(left) {
            left->setView("front");
            left->setView("side");
            left->setView("top");
            left->setView("iso");
            CHECK(left->isVisibleTo(&h40),
                  "Haunch 40: orbit/front/side/top main view stays active");
            auto* pitch=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("upperBoltPitchX"));
            auto* edgeL=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("boltEdgeLeft"));
            auto* edgeR=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("boltEdgeRight"));
            CHECK(pitch && edgeL && edgeR,
                  "P4G: upper pitch and shared side margins found");
            if(pitch && edgeL && edgeR) {
                pitch->setValue(150.0);
                edgeL->setValue(42.0);
                edgeR->setValue(62.0);
                QApplication::processEvents();
                const auto upper=left->boltOverlayMetrics("haunch_bolts_upper");
                const auto lower=left->boltOverlayMetrics("haunch_bolts_lower");
                CHECK(upper.valid && lower.valid &&
                      std::fabs(upper.pitchX-150.0)<1e-6 &&
                      std::fabs(lower.pitchX-120.0)<1e-6 &&
                      std::fabs(upper.leftEdge-42.0)<1e-6 &&
                      std::fabs(upper.rightEdge-62.0)<1e-6 &&
                      left->previewPlan().ok,
                      "P4G: upper pitch edit leaves lower pitch independent");
            }
            CHECK(!left->boltOverlayMetrics("haunch_web").valid,
                  "P4B: no bolt dimensions attached to non-bolt role");
        }

        // Changing nominal bolt size must not destroy four synchronized
        // previews because the old hole was smaller than the new bolt.
        auto* boltDiameter=h40.findChild<QDoubleSpinBox*>(
            QStringLiteral("upperBoltDiameter"));
        auto* holeDiameter=h40.findChild<QDoubleSpinBox*>(
            QStringLiteral("upperHoleDiameter"));
        auto* boltPitchY=h40.findChild<QDoubleSpinBox*>(
            QStringLiteral("upperBoltPitchY"));
        auto* haunchDepth=h40.findChild<QDoubleSpinBox*>(
            QStringLiteral("haunchDepth"));
        auto* tipDepth=h40.findChild<QDoubleSpinBox*>(
            QStringLiteral("haunchTipDepth"));
        CHECK(boltDiameter && holeDiameter && boltPitchY && haunchDepth && tipDepth,
              "Haunch 40: dependent property editors are addressable");
        if(boltDiameter && holeDiameter && boltPitchY && haunchDepth && tipDepth) {
            boltDiameter->setValue(25.0);
            QApplication::processEvents();
            CHECK(std::fabs(holeDiameter->value()-27.0)<0.001 &&
                  holeDiameter->minimum()>=27.0,
                  "P4: M25 automatically requires >=27mm physical hole");
            CHECK(left && left->previewPlan().ok && left->previewPlan().boltGroups.size()==2,
                  "P4: enlarged bolt and hole preserve native live preview");
            // An invalid manual hole entry is clamped, not accepted and
            // only rejected later by ComponentRegistry during repaint.
            holeDiameter->setValue(22.0);
            QApplication::processEvents();
            CHECK(holeDiameter->value()>=27.0,
                  "P4: hole editor cannot shrink below live bolt clearance");
            boltPitchY->setValue(500.0);
            QApplication::processEvents();
            CHECK(boltPitchY->value()<500.0 && boltPitchY->value()>27.0 &&
                  left && left->previewPlan().ok,
                  "P4: vertical bolt pitch clamps to actual beam/haunch fit");
            tipDepth->setValue(50.0);
            haunchDepth->setValue(60.0);
            QApplication::processEvents();
            CHECK(haunchDepth->value()>75.0 && left && left->previewPlan().ok,
                  "P4: wedge depth remains strictly above tip + 25 mm");
            if(left)for(auto* p:previews)
                CHECK(p->previewPlan().ok,
                      "P4: main 3D and all 3 orthographic plans stay valid");
            // Loading a different component's saved parameters must clear
            // limits inherited from the previously edited connection.
            h40.setParams({
                {L"boltDiameter",mm::CompParamValue{36.0}},
                {L"holeDiameter",mm::CompParamValue{38.0}}
            });
            h40.setParams({
                {L"boltDiameter",mm::CompParamValue{20.0}},
                {L"holeDiameter",mm::CompParamValue{22.0}}
            });
            QApplication::processEvents();
            CHECK(std::fabs(boltDiameter->value()-20.0)<0.001 &&
                  std::fabs(holeDiameter->value()-22.0)<0.001 &&
                  left && left->previewPlan().ok,
                  "P4: Get restores 20/22 without retaining old M36 bounds");

            // Reproduce the user's saved-dialog screenshot directly: loaded
            // values are M25 and old hole=22. Loading must repair the draft
            // and all four previews without changing the real Document.
            h40.setParams({
                {L"boltDiameter",mm::CompParamValue{25.0}},
                {L"holeDiameter",mm::CompParamValue{22.0}}
            });
            QApplication::processEvents();
            CHECK(std::fabs(holeDiameter->value()-27.0)<0.001 &&
                  left && left->previewPlan().ok,
                  "P4: existing M25/22 dialog loads as valid M25/27 draft");
            for(auto* p:previews)
                CHECK(p->previewPlan().ok,
                      "P4: loaded saved dimensions keep 3D/orthographic previews");

            // Independent upper and lower CLEAR EDGE offsets. Geometry
            // must move only the selected bolt group, not the end plate.
            h40.setParams({});
            QApplication::processEvents();
            auto* manualH=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("endPlateHeight"));
            auto* manualW=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("endPlateWidth"));
            auto* upperClear=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("upperBoltEdgeClear"));
            auto* lowerClear=h40.findChild<QDoubleSpinBox*>(
                QStringLiteral("lowerBoltEdgeClear"));
            CHECK(manualH && manualW && upperClear && lowerClear,
                  "P4G: independent net edge and plate dimension editors exist");
            if(manualH && manualW && upperClear && lowerClear && left) {
                const auto first=left->previewPlan();
                CHECK(first.ok && first.boltGroups.size()==2 && !first.plates.empty(),
                      "P4G: base plan has independent upper and lower groups");
                const auto v=first.plates[0].v;
                const auto along=[&](mm::Vec3 a,mm::Vec3 b) {
                    const mm::Vec3 d=a-b;
                    return d.x*v.x+d.y*v.y+d.z*v.z;
                };
                upperClear->setValue(64.0);  // 44 -> 64, 20 mm downward
                QApplication::processEvents();
                const auto changedTop=left->previewPlan();
                CHECK(changedTop.ok && first.ok &&
                      std::fabs(changedTop.plates[0].height-first.plates[0].height)<1e-6 &&
                      std::fabs(along(changedTop.plates[0].center,first.plates[0].center))<1e-6 &&
                      std::fabs(along(changedTop.boltGroups[0].origin,
                                      first.boltGroups[0].origin)+20.0)<1e-6 &&
                      std::fabs(along(changedTop.boltGroups[1].origin,
                                      first.boltGroups[1].origin))<1e-6 &&
                      std::fabs(left->boltOverlayMetrics("haunch_bolts_upper").topEdge-
                                (64.0+changedTop.boltGroups[0].params.holeDiameter*0.5))<1e-6,
                      "P4G: upper clear edge moves only upper bolts, plate stays fixed");
                lowerClear->setValue(59.0);  // 44 -> 59, 15 mm upward
                QApplication::processEvents();
                const auto changedLower=left->previewPlan();
                CHECK(changedLower.ok && changedTop.ok &&
                      std::fabs(changedLower.plates[0].height-
                                changedTop.plates[0].height)<1e-6 &&
                      std::fabs(along(changedLower.boltGroups[0].origin,
                                      changedTop.boltGroups[0].origin))<1e-6 &&
                      std::fabs(along(changedLower.boltGroups[1].origin,
                                      changedTop.boltGroups[1].origin)-15.0)<1e-6,
                      "P4G: lower clear edge moves only lower bolts");
                // Confirm 3D edits use the same independent form field.
                left->selectRole("haunch_bolts_upper");
                QApplication::processEvents();
                QDoubleSpinBox* top3d=nullptr;
                for(auto* sp:left->findChildren<QDoubleSpinBox*>())
                    if(sp->toolTip()==QStringLiteral("upperBoltEdgeClear"))
                        top3d=sp;
                CHECK(top3d && top3d->isVisibleTo(left),
                      "P4G: upper 3D clear-edge input visible");
                if(top3d) {
                    const auto before=left->previewPlan();
                    top3d->setValue(54.0);
                    QApplication::processEvents();
                    const auto after=left->previewPlan();
                    CHECK(after.ok && std::fabs(upperClear->value()-54.0)<1e-6 &&
                          std::fabs(after.plates[0].height-before.plates[0].height)<1e-6 &&
                          std::fabs(along(after.boltGroups[0].origin,
                                          before.boltGroups[0].origin)-10.0)<1e-6 &&
                          std::fabs(along(after.boltGroups[1].origin,
                                          before.boltGroups[1].origin))<1e-6,
                          "P4G: 3D upper clear edge moves upper only, synchronizes panel");
                }
                left->selectRole("haunch_bolts_lower");
                QApplication::processEvents();
                QDoubleSpinBox* bottom3d=nullptr;
                for(auto* sp:left->findChildren<QDoubleSpinBox*>())
                    if(sp->toolTip()==QStringLiteral("lowerBoltEdgeClear"))
                        bottom3d=sp;
                CHECK(bottom3d && bottom3d->isVisibleTo(left),
                      "P4G: lower 3D clear-edge input visible");
                if(bottom3d) {
                    bottom3d->setValue(49.0);
                    QApplication::processEvents();
                    CHECK(std::fabs(lowerClear->value()-49.0)<1e-6 &&
                          left->previewPlan().ok,
                          "P4G: 3D lower clear edge synchronizes lower panel only");
                }
                // A shorter plate is a separate explicit user decision,
                // and remains independent of bolt-group positioning edits.
                h40.setParams({});
                manualH->setValue(350.0);
                QApplication::processEvents();
                CHECK(std::fabs(manualH->value()-350.0)<0.01 &&
                      left->previewPlan().ok &&
                      std::fabs(left->previewPlan().plates[0].height-350.0)<0.01,
                      "P4G: manual 350mm plate allowed with independent groups");
                upperClear->setValue(34.0);
                lowerClear->setValue(64.0);
                QApplication::processEvents();
                const auto compact=left->previewPlan();
                CHECK(compact.ok && std::fabs(manualH->value()-350.0)<0.01 &&
                      std::fabs(compact.plates[0].height-350.0)<0.01 &&
                      std::fabs(left->boltOverlayMetrics("haunch_bolts_upper").topEdge-
                                45.0)<1e-6 &&
                      std::fabs(left->boltOverlayMetrics("haunch_bolts_lower").bottomEdge-
                                75.0)<1e-6,
                      "P4G: net 34/64 means centre 45/75, plate height unchanged");
                manualW->setValue(165.0);
                QApplication::processEvents();
                CHECK(left->previewPlan().ok &&
                      std::fabs(left->previewPlan().plates[0].width-165.0)<0.01,
                      "P4G: manual plate width remains independently editable");
            }
            h40.setParams({{L"boltEdge",mm::CompParamValue{50.0}}});
            QApplication::processEvents();
            if(upperClear && lowerClear && left) {
                const auto up=left->boltOverlayMetrics("haunch_bolts_upper");
                const auto lo=left->boltOverlayMetrics("haunch_bolts_lower");
                CHECK(std::fabs(upperClear->value()-59.0)<1e-6 &&
                      std::fabs(lowerClear->value()-59.0)<1e-6 &&
                      up.valid && lo.valid &&
                      std::fabs(up.topEdge-70.0)<1e-6 &&
                      std::fabs(lo.bottomEdge-70.0)<1e-6,
                      "P4G: saved boltEdge=50 maps to 59mm net/70mm centre offsets");
            }
        }
    }

    // 3) KOYU TEMA: aktif alanlarin yazisi siyah olmali (beyaz kutuda beyaz yazi
    //    gorunmuyordu) ve odaklanan alan arka plani degistirmez (yazi vurgulanir).
    {
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(32, 32, 32));
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, QColor(45, 45, 45));
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::ButtonText, Qt::white);
        QApplication::setPalette(dark);
        mm::ComponentDialogWidget wd(mm::shearPlate146DialogDefinition(), {});
        wd.resize(900, 700);
        wd.show();
        QApplication::processEvents();
        int checked = 0, withDarkText = 0;
        for (auto* e : wd.findChildren<QAbstractSpinBox*>()) {
            if (!e->isEnabled() || !e->isVisibleTo(&wd)) continue;
            const QImage img = e->grab().toImage();
            bool dark_px = false;
            for (int y = 0; y < img.height() && !dark_px; ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qGray(img.pixel(x, y)) < 70) { dark_px = true; break; }
            ++checked;
            if (dark_px) ++withDarkText;
            if (checked >= 6) break;
        }
        CHECK(checked > 0 && withDarkText == checked,
              "koyu temada aktif alan yazisi siyah (gorunur)");
        // Odak: arka plan ayni kalir (turuncu satir yok).
        auto* anySpin = wd.findChildren<QAbstractSpinBox*>().isEmpty()
                            ? nullptr
                            : wd.findChildren<QAbstractSpinBox*>().first();
        if (anySpin && anySpin->isEnabled()) {
            anySpin->setFocus();
            QApplication::processEvents();
            CHECK(!anySpin->styleSheet().contains(QStringLiteral("FFE3B3")),
                  "odakta turuncu satir/kutu zemini yok");
        }
        QApplication::setPalette(QApplication::style()->standardPalette());
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("component_dialog_widget: TUM KONTROLLER GECTI\n");
    return 0;
}
