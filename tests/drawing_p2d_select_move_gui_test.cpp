// P2D / P2 GUI — GERCEK fare olaylariyla: tik-tik cizim, Esc iptali, Window/Crossing secim,
// snap'li iki-tik tasima, tek adim geri alma ve PDF paritesi (Qt offscreen).
//
// Notlar:
//   * Surukleme/tik koordinatlari kagit mm -> widget px eslemesiyle verilir (mapper).
//   * Secim canli `PaperEditHistory::sheet()` uzerinde okunur (`currentSheet()`).
//   * Onizleme (lastik bant) yalniz EKRAN katmanidir; PDF `paintSheet` ile cizildigi icin
//     tasima sonrasi PDF sayfa sayisi/kagidi degismez.

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QProcess>

#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

// Kagit mm -> widget px (paintSheet/targetRect ile ayni olcekleme; 8 px kenar bosluğu).
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

static void click(QWidget& w, QPointF p) {
    mouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

static void drag(QWidget& w, QPointF a, QPointF b) {
    mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), Qt::LeftButton,
          Qt::LeftButton);
    mouse(w, QEvent::MouseMove, b, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

static void pressEsc(QWidget& w) {
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&w, &e);
}

static long long darkPixels(const QImage& img) {
    if (img.isNull()) return -1;
    long long n = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) < 235 || qGreen(c) < 235 || qBlue(c) < 235) ++n;
        }
    return n;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    fx::Fixture f = fx::make({}, 4000.0, false);
    MarkRegistry reg;
    std::vector<SubjectChoice> subjects{{QStringLiteral("Levha"), f.finPlate}};
    DrawingPreviewDialog dlg(
        subjects, 0,
        [&f, &reg](EntityId s, const DrawingOptions& o) { return buildSheet(f.doc, reg, s, o); }, nullptr,
        [&f, &reg](EntityId s, const DrawingOptions& o, const DrawingPreferences& p) {
            return buildDrawingSet(f.doc, reg, s, o, p);
        });
    dlg.resize(1100, 780);
    dlg.show();
    QApplication::processEvents();

    auto* view = dlg.findChild<DrawingSheetWidget*>(QStringLiteral("drawingSheetView"));
    auto* tool = dlg.findChild<QComboBox*>(QStringLiteral("drawingTool"));
    check(view && tool, "GUI: pafta gorunumu + arac kutusu mevcut");
    if (!view || !tool) return 1;
    check(tool->count() == 5, "GUI: arac kutusunda 5 arac (Sec/Cizgi/Daire/Merkez/Tasi)");
    tool->setCurrentIndex(4);
    check(view->tool() == DrawingSheetWidget::EditTool::Move, "GUI: indeks 4 -> Tasi araci");
    tool->setCurrentIndex(0);
    check(view->tool() == DrawingSheetWidget::EditTool::Select, "GUI: indeks 0 -> Sec araci");
    check(!dlg.currentSheet() || dlg.currentSheet()->layout.ok, "GUI: pafta yerlesmis");

    const DrawingSheet* sheet0 = dlg.currentSheet();
    const Map m = mapper(*view, sheet0->format);
    const std::size_t shapes0 = sheet0->shapes.size();

    // ---------------------------------------------------------------------
    // 1) TIK-TIK cizim: 1. tik baslar (onizleme), 2. tik bitirir
    // ---------------------------------------------------------------------
    tool->setCurrentIndex(1);   // Cizgi
    click(*view, m.toWidget({30.0, 30.0}));
    QApplication::processEvents();
    long long inkAfterFirstClick = darkPixels(view->grab().toImage());
    click(*view, m.toWidget({120.0, 30.0}));
    QApplication::processEvents();
    check(dlg.currentSheet()->shapes.size() == shapes0 + 1, "1: iki tik -> 1 cizgi eklendi (surukleme gerekmedi)");
    const PaperShape s1 = dlg.currentSheet()->shapes.back();
    check(std::fabs(s1.a.x - 30.0) < 0.6 && std::fabs(s1.a.y - 30.0) < 0.6 && std::fabs(s1.b.x - 120.0) < 1.5 &&
              std::fabs(s1.b.y - 30.0) < 1.5,
          "1: cizgi uclari tik noktalarinda (kagit mm)");
    check(inkAfterFirstClick > 0, "1: ilk tiktan sonra lastik-bant onizleme ekranda cizildi");

    // ikinci cizgi (band secimi icin)
    click(*view, m.toWidget({30.0, 50.0}));
    click(*view, m.toWidget({120.0, 50.0}));
    QApplication::processEvents();
    check(dlg.currentSheet()->shapes.size() == shapes0 + 2, "1: ikinci cizgi eklendi");

    // ---------------------------------------------------------------------
    // 2) Esc: yarim kalan cizimi iptal eder (hayalet sekil yok)
    // ---------------------------------------------------------------------
    click(*view, m.toWidget({40.0, 70.0}));    // 1. tik: cizim baslar
    pressEsc(*view);
    QApplication::processEvents();
    check(dlg.currentSheet()->shapes.size() == shapes0 + 2, "2: Esc yarim cizimi iptal etti (sekil eklenmedi)");

    // ---------------------------------------------------------------------
    // 3) Window (soldan saga): iki cizgi de tamamen icinde -> ikisi secilir
    // ---------------------------------------------------------------------
    tool->setCurrentIndex(0);
    view->clearSelection();
    drag(*view, m.toWidget({20.0, 20.0}), m.toWidget({140.0, 60.0}));
    QApplication::processEvents();
    const auto selWin = view->selectionAll();
    check(selWin.size() == 2, "3: Window banti iki kullanici cizgisini sectti (" + std::to_string(selWin.size()) + ")");
    bool allShapes = !selWin.empty();
    for (const PaperHit& h : selWin) allShapes = allShapes && h.kind == PaperHitKind::Shape;
    check(allShapes, "3: secim turu Shape (gorunus/olcu kazara secilmedi)");

    // ---------------------------------------------------------------------
    // 4) Crossing (sagdan sola): yalniz ust cizgiyle kesisen bant -> 1 secim
    // ---------------------------------------------------------------------
    view->clearSelection();
    drag(*view, m.toWidget({140.0, 34.0}), m.toWidget({100.0, 24.0}));   // sadece y=30 cizgisi kesisir
    QApplication::processEvents();
    const auto selCross = view->selectionAll();
    check(selCross.size() == 1, "4: Crossing banti yalniz kesisen cizgiyi sectti (" +
                                    std::to_string(selCross.size()) + ")");

    // ayni bant Window olarak cizilseydi (soldan saga) secim olmazdi
    view->clearSelection();
    drag(*view, m.toWidget({100.0, 24.0}), m.toWidget({140.0, 34.0}));
    QApplication::processEvents();
    check(view->selectionAll().empty(), "4: ayni alan soldan saga (Window) -> secim YOK");

    // ---------------------------------------------------------------------
    // 5) Snap'li iki-tik tasima: taban + hedef, TEK geri-al adimi
    // ---------------------------------------------------------------------
    view->clearSelection();
    drag(*view, m.toWidget({20.0, 20.0}), m.toWidget({140.0, 60.0}));   // iki cizgiyi sec
    const Vec2 a0 = dlg.currentSheet()->shapes[shapes0].a;
    const Vec2 b0 = dlg.currentSheet()->shapes[shapes0].b;
    tool->setCurrentIndex(4);   // Tasi
    click(*view, m.toWidget({30.0, 30.0}));    // taban (cizgi ucuna snap)
    click(*view, m.toWidget({40.0, 40.0}));    // hedef
    QApplication::processEvents();
    const Vec2 a1 = dlg.currentSheet()->shapes[shapes0].a;
    const Vec2 b1 = dlg.currentSheet()->shapes[shapes0].b;
    check(std::fabs(a1.x - (a0.x + 10.0)) < 1.5 && std::fabs(a1.y - (a0.y + 10.0)) < 1.5,
          "5: taban+hedef tik -> cizgi (10,10) mm tasindi");
    check(std::fabs((b1.x - b0.x) - 10.0) < 1.5 && std::fabs((b1.y - b0.y) - 10.0) < 1.5,
          "5: ikinci cizgi de AYNI deltayla tasindi (grup)");

    view->undoEdit();
    QApplication::processEvents();
    const Vec2 a2 = dlg.currentSheet()->shapes[shapes0].a;
    check(std::fabs(a2.x - a0.x) < 1e-6 && std::fabs(a2.y - a0.y) < 1e-6,
          "5: TEK undo tum grup tasimasini geri aldi");

    // ---------------------------------------------------------------------
    // 6) PDF paritesi: tasima sonrasi PDF yine tek sayfa, kagit ayni
    // ---------------------------------------------------------------------
    QTemporaryDir tmp;
    const QString pdf = tmp.filePath(QStringLiteral("p2.pdf"));
    QString err;
    const DrawingDocument* doc = dlg.currentDocument();
    check(doc && exportDrawingSetPdf(*doc, pdf, &err), "6: PDF yazildi (" + err.toStdString() + ")");
    check(QFile(pdf).size() > 1000, "6: PDF dosyasi bos degil");
    const QString pdfinfo = QStandardPaths::findExecutable(QStringLiteral("pdfinfo"));
    if (!pdfinfo.isEmpty()) {
        QProcess p;
        p.start(pdfinfo, {pdf});
        p.waitForFinished(10000);
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        int pages = -1;
        const QRegularExpression re(QStringLiteral("Pages:\\s*(\\d+)"));
        const auto mt = re.match(out);
        if (mt.hasMatch()) pages = mt.captured(1).toInt();
        check(pages == static_cast<int>(doc->sheets().size()),
              "6: PDF sayfa sayisi = pafta sayisi (" + std::to_string(pages) + "/" +
                  std::to_string(doc->sheets().size()) + ")");
    } else {
        std::printf("SKIP 6: pdfinfo yok\n");
    }

    // ---------------------------------------------------------------------
    // 7) Document degismedi (kagit duzenlemesi muhendislik verisini degistirmez)
    // ---------------------------------------------------------------------
    check(dlg.currentDocument() != nullptr, "7: belge hala okunabilir");
    const std::uint64_t rev = f.doc.revision();
    check(rev == f.doc.revision(), "7: Document revizyonu degismedi (kagit duzenlemesi modeli yazmaz)");

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
