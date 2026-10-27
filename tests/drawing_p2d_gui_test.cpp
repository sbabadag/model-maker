// P2D GUI testi (Qt offscreen): kagit secenegi degisince cizim yeniden uretilir ve KULLANICI
// PAFTA DUZENLEMELERI korunur (preservePaperEdits/mergePaperEdits); yikici degisiklik (konu)
// ACIK ONAY ister; durum satiri kagit/olcek/sebep yazar; PDF/JSON cikti deterministik.

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static bool contains(const QString& hay, const QString& needle) { return hay.contains(needle); }

static fx::Fixture makeAssembly(double len, int extraPlates) {
    fx::Fixture f = fx::make({}, len, true);
    for (int i = 0; i < extraPlates; ++i) {
        const double x = 200.0 + 130.0 * i;
        const EntityId p = fx::addFramedPlate(f, {x, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10,
                                              ("gui_levha_" + std::to_string(i)).c_str());
        fx::addBolts(f, p, {x - 100, 80, 30}, {0, 1, 0}, {1, 0, 0}, 3, 2, 100, 60, 22);
        Weld w;
        w.mainPart = f.beam;
        w.secondaryPart = p;
        w.weldClass = WeldClass::Shop;
        f.doc.addWeld(w);
    }
    return f;
}

struct Map {
    QRectF base;
    double s{1.0};
    double ox{0.0}, oy{0.0};
    QPointF toWidget(Vec2 paperMm) const { return {ox + paperMm.x * s, oy - paperMm.y * s}; }
};
static Map mapper(const QWidget& w, const SheetFormat& f) {
    Map m;
    m.base = QRectF(w.rect()).adjusted(8, 8, -8, -8);
    m.s = std::min(m.base.width() / f.width, m.base.height() / f.height);
    m.ox = m.base.center().x() - f.width * m.s / 2.0;
    m.oy = m.base.center().y() + f.height * m.s / 2.0;
    return m;
}
static void mouse(QWidget& w, QEvent::Type type, QPointF local, Qt::MouseButton btn, Qt::MouseButtons btns) {
    QMouseEvent e(type, local, w.mapToGlobal(local.toPoint()), btn, btns, Qt::NoModifier);
    QApplication::sendEvent(&w, &e);
}
static void drag(QWidget& w, QPointF a, QPointF b) {
    mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, b, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

struct DimKey {
    DimensionRole role{DimensionRole::OverallWidth};
    std::string featureKey;
    ElementId chain{0};
    DimensionDirection dir{DimensionDirection::Horizontal};
};
static bool sameKey(const Dimension2D& d, const DimKey& k) {
    return d.role == k.role && d.featureKey == k.featureKey && d.chain == k.chain && d.direction == k.dir;
}
static const Dimension2D* findDim(const DrawingSheet& sheet, const DimKey& k) {
    for (const Dimension2D& d : sheet.dimensions)
        if (sameKey(d, k)) return &d;
    return nullptr;
}
static const Dimension2D* findDimAnywhere(const DrawingDocument& doc, const DimKey& k) {
    for (const DrawingSheet& s : doc.sheets())
        if (const Dimension2D* d = findDim(s, k)) return d;
    return nullptr;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    fx::Fixture f = makeAssembly(2400.0, 2);
    const std::uint64_t rev0 = f.doc.revision();
    const std::size_t models0 = f.doc.models().size();
    MarkRegistry reg;
    const EntityId asmId = f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id;
    std::vector<SubjectChoice> subjects{{QStringLiteral("Levha"), f.finPlate}, {QStringLiteral("Assembly"), asmId}};
    DrawingPreviewDialog dlg(
        subjects, 1,
        [&f, &reg](EntityId s, const DrawingOptions& o) { return buildSheet(f.doc, reg, s, o); }, nullptr,
        [&f, &reg](EntityId s, const DrawingOptions& o, const DrawingPreferences& p) {
            return buildDrawingSet(f.doc, reg, s, o, p);
        });
    bool confirmAnswer = false;
    int confirmCalls = 0;
    dlg.setRebuildConfirmation([&](const QString&) {
        ++confirmCalls;
        return confirmAnswer;
    });
    dlg.resize(1100, 780);
    dlg.show();
    QApplication::processEvents();

    auto* view = dlg.findChild<DrawingSheetWidget*>(QStringLiteral("drawingSheetView"));
    auto* tool = dlg.findChild<QComboBox*>(QStringLiteral("drawingTool"));
    auto* sizeCombo = dlg.findChild<QComboBox*>(QStringLiteral("drawingSize"));
    auto* autoSize = dlg.findChild<QCheckBox*>(QStringLiteral("drawingAutoSize"));
    auto* subjectCombo = dlg.findChild<QComboBox*>(QStringLiteral("drawingSubject"));
    auto* status = dlg.findChild<QLabel*>(QStringLiteral("drawingStatus"));
    check(view && tool && sizeCombo && autoSize && subjectCombo && status,
          "GUI: gorunum, arac, kagit, otomatik kagit, konu ve durum satiri mevcut");
    if (!view || !tool || !sizeCombo || !autoSize || !subjectCombo || !status) return 1;
    check(dlg.currentDocument() && dlg.currentSheet(), "GUI: baslangic cizimi uretildi");
    if (!dlg.currentDocument() || !dlg.currentSheet()) return 1;

    // 1) Kullanici duzenlemesi: kagida 2B cizgi ekle (duzenleme izi).
    tool->setCurrentIndex(1);   // Cizgi
    QApplication::processEvents();
    const Map m0 = mapper(*view, dlg.currentSheet()->format);
    drag(*view, m0.toWidget({16.0, dlg.currentSheet()->format.height - 16.0}),
         m0.toWidget({60.0, dlg.currentSheet()->format.height - 26.0}));
    QApplication::processEvents();
    const std::size_t shapesAfterDraw = dlg.currentSheet()->shapes.size();
    check(shapesAfterDraw == 1, "GUI: kullanici 2B cizgisi eklendi (duzenleme)");

    // 2) Olcu metnini fare ile tasi -> olcu YERLESIMI degisir (DEGER degismez).
    tool->setCurrentIndex(0);   // Sec
    QApplication::processEvents();
    DimKey editedKey;
    bool haveKey = false;
    double beforeOffset = 0.0;
    const Map mm = mapper(*view, dlg.currentSheet()->format);
    for (const Dimension2D& d : dlg.currentSheet()->dimensions) {
        if (d.featureKey.empty()) continue;
        const DimensionGeometry g = resolveDimension(*dlg.currentSheet(), d);
        if (!g.valid || g.textBox.hx <= 0.0) continue;
        const QPointF from = mm.toWidget(g.textCentre);
        // Once SADECE bas: hit-test gercekten bu olcuyu mu secti? (metin/mark onunde olabilir)
        mouse(*view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        if (view->selection().kind != PaperHitKind::Dimension) {
            mouse(*view, QEvent::MouseButtonRelease, from, Qt::LeftButton, Qt::NoButton);
            QApplication::processEvents();
            continue;
        }
        editedKey = {d.role, d.featureKey, d.chain, d.direction};
        beforeOffset = d.offset;
        haveKey = true;
        const QPointF to = from + QPointF(0.0, -6.0 * mm.s);
        mouse(*view, QEvent::MouseMove, QPointF((from.x() + to.x()) / 2.0, (from.y() + to.y()) / 2.0), Qt::LeftButton,
              Qt::LeftButton);
        mouse(*view, QEvent::MouseMove, to, Qt::LeftButton, Qt::LeftButton);
        mouse(*view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
        break;
    }
    check(haveKey, "GUI: tasinacak imalat olcusu bulundu");
    const Dimension2D* movedDim = haveKey ? findDim(*dlg.currentSheet(), editedKey) : nullptr;
    check(movedDim != nullptr && std::fabs(movedDim->offset - beforeOffset) > 0.5,
          "GUI: olcu metni tasindi (offset degisti, deger degismedi)");
    const double editedOffset = movedDim ? movedDim->offset : beforeOffset;
    const double editedValue = movedDim ? movedDim->value : 0.0;

    // 3) Kagit/secenek degisikligi: otomatik kagit kapat -> A3; duzenlemeler KORUNUR.
    autoSize->setChecked(false);
    QApplication::processEvents();
    check(dlg.currentDocument() != nullptr && dlg.currentSheet() != nullptr, "GUI: secenek degisikligi yeniden uretti");
    check(dlg.currentSheet()->format.size == SheetSize::A3, "GUI: sabit A3 secildi (yeniden hesaplandi)");
    check(dlg.currentSheet()->shapes.size() == 1, "GUI: 2B cizgi kagit degisiminde KORUNDU");
    const Dimension2D* after1 = haveKey ? findDimAnywhere(*dlg.currentDocument(), editedKey) : nullptr;
    check(after1 && std::fabs(after1->offset - editedOffset) < 1e-6 &&
              std::fabs(after1->value - editedValue) < 1e-9,
          "GUI: tasinan olcunun YERLESIMI korundu, DEGERI degismedi");
    check(contains(status->text(), QStringLiteral("korundu")), "GUI: durum satiri duzenlemelerin korundugunu yazar");
    check(contains(status->text(), QStringLiteral("A3")) && contains(status->text(), QStringLiteral("1:")),
          "GUI: durum satiri secilen kagit + olcegi yazar");

    // 4) Kagit A2'ye buyut: yine yeniden hesaplama + duzenleme korunumu + coklu/tek pafta sebebi.
    sizeCombo->setCurrentIndex(2);   // A2
    QApplication::processEvents();
    check(dlg.currentSheet()->format.size == SheetSize::A2, "GUI: kagit A2'ye degisti");
    check(dlg.currentSheet()->shapes.size() == 1, "GUI: 2B cizgi ikinci kagit degisiminde de korundu");
    const Dimension2D* after2 = haveKey ? findDimAnywhere(*dlg.currentDocument(), editedKey) : nullptr;
    check(after2 && std::fabs(after2->offset - editedOffset) < 1e-6, "GUI: olcu yerlesimi A2 gecisinde korundu");
    check(contains(status->text(), QStringLiteral("pafta")) && contains(status->text(), QStringLiteral("A2")),
          "GUI: durum satiri coklu/tek pafta + kagit bilgisini yazar");

    // 5) Yikici degisiklik (konu) REDDEDILIRSE: hicbir sey kaybolmaz, konu geri doner.
    confirmAnswer = false;
    confirmCalls = 0;
    const std::size_t pagesBefore = dlg.currentDocument()->sheets().size();
    subjectCombo->setCurrentIndex(0);
    QApplication::processEvents();
    check(confirmCalls == 1, "GUI: konu degisikligi (yikici) ACIK ONAY istedi");
    check(subjectCombo->currentIndex() == 1, "GUI: onay reddedilince konu eski haline dondu");
    check(dlg.currentDocument()->sheets().size() == pagesBefore && dlg.currentSheet()->shapes.size() == 1,
          "GUI: onay reddedilince cizim/duzenlemeler AYNEN korundu");

    // 6) Onay VERILIRSE kaynak eslesmeyen duzenlemeler birakilir (kullanici bilerek kabul etti).
    confirmAnswer = true;
    subjectCombo->setCurrentIndex(0);
    QApplication::processEvents();
    check(dlg.currentDocument() != nullptr && dlg.currentDocument()->sheets()[0].subject != asmId,
          "GUI: onay verilince yeni konu cizildi (kaynak eslesmeyen duzenleme tasinmadi)");

    // 7) PDF/JSON cikti deterministik (ayni set iki kez ayni icerik).
    MarkRegistry reg2;
    DrawingOptions o2;
    o2.layout.fixedScaleDen = 5.0;
    DrawingPreferences p2;
    p2.autoSheetSize = false;
    p2.sheetSize = SheetSize::A2;
    const DrawingSetResult r1 = buildDrawingSet(f.doc, reg2, asmId, o2, p2);
    MarkRegistry reg3;
    const DrawingSetResult r2 = buildDrawingSet(f.doc, reg3, asmId, o2, p2);
    check(r1.document.serialize() == r2.document.serialize(), "GUI/JSON: iki uretim birebir ayni JSON");
    DrawingDocument back;
    std::string err;
    check(DrawingDocument::deserialize(r1.document.serialize(), back, err) &&
              back.serialize() == r1.document.serialize(),
          "GUI/JSON: round-trip birebir (deterministik)");

    QTemporaryDir tmp;
    const QString pdf1 = tmp.path() + QStringLiteral("/set1.pdf");
    const QString pdf2 = tmp.path() + QStringLiteral("/set2.pdf");
    check(exportDrawingSetPdf(r1.document, pdf1) && exportDrawingSetPdf(r1.document, pdf2),
          "GUI/PDF: coklu pafta PDF yazildi (iki kez)");
    QProcess pdfinfo;
    pdfinfo.start(QStringLiteral("pdfinfo"), {pdf1});
    const bool havePdfInfo = pdfinfo.waitForFinished(60000) && pdfinfo.exitCode() == 0;
    if (havePdfInfo) {
        const QString info = QString::fromLocal8Bit(pdfinfo.readAllStandardOutput());
        const QStringList lines = info.split('\n');
        int pages = -1;
        for (const QString& l : lines)
            if (l.startsWith(QStringLiteral("Pages:"))) pages = l.section(':', 1).trimmed().toInt();
        check(pages == static_cast<int>(r1.document.sheets().size()),
              "GUI/PDF: PDF sayfa sayisi pafta sayisina esit (her pafta bir sayfa)");
    } else {
        check(QFileInfo(pdf1).size() > 0, "GUI/PDF: PDF yazildi (pdfinfo yok, sayfa sayisi dogrulanamadi)");
    }
    QProcess t1;
    t1.start(QStringLiteral("pdftotext"), {pdf1, QStringLiteral("-")});
    QProcess t2;
    t2.start(QStringLiteral("pdftotext"), {pdf2, QStringLiteral("-")});
    const bool ok1 = t1.waitForFinished(60000) && t1.exitCode() == 0;
    const bool ok2 = t2.waitForFinished(60000) && t2.exitCode() == 0;
    if (ok1 && ok2) {
        const QByteArray a = t1.readAllStandardOutput();
        const QByteArray b = t2.readAllStandardOutput();
        check(!a.isEmpty() && a == b, "GUI/PDF: ayni set -> ayni metin icerigi (deterministik cikti)");
    } else {
        check(QFileInfo(pdf1).size() == QFileInfo(pdf2).size(),
              "GUI/PDF: pdftotext yok, dosya boyutlari ayni (kaba determinizm)");
    }

    check(f.doc.revision() == rev0 && f.doc.models().size() == models0,
          "GUI: tum GUI islemleri boyunca Document DEGISMEDI");

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
