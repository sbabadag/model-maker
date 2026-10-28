// P2B GUI testi (Qt offscreen): paftada fare ile 2B cizgi cizme, secme/tasima, geri al/yinele,
// gizleme -> INCOMPLETE, ayni planin ekran ve PDF'te cizilmesi (vektor PDF raster denetimi).

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>

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

static long long inkCount(const QImage& img) {
    if (img.isNull()) return -1;
    long long dark = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) < 230 || qGreen(c) < 230 || qBlue(c) < 230) ++dark;
        }
    return dark;
}

// Iki raster arasindaki farkli piksel sayisi (eklenen cizgi ince olabilir; esik yerine fark).
static long long diffCount(const QImage& a, const QImage& b) {
    if (a.isNull() || b.isNull() || a.size() != b.size()) return -1;
    long long n = 0;
    for (int y = 0; y < a.height(); y += 2)
        for (int x = 0; x < a.width(); x += 2)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
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

static void click(QWidget& w, QPointF p) {
    mouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

static void drag(QWidget& w, QPointF a, QPointF b) {
    mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseMove, b, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    fx::Fixture f = fx::make({}, 4000.0, false);
    const std::uint64_t rev0 = f.doc.revision();
    MarkRegistry reg;
    std::vector<SubjectChoice> subjects{{QStringLiteral("Levha"), f.finPlate}};
    DrawingPreviewDialog dlg(
        subjects, 0,
        [&f, &reg](EntityId s, const DrawingOptions& o) { return buildSheet(f.doc, reg, s, o); },
        nullptr,
        [&f, &reg](EntityId s, const DrawingOptions& o, const DrawingPreferences& p,
                   const ProgressCallback& progress) {
            return buildDrawingSet(f.doc, reg, s, o, p, progress);
        });
    dlg.resize(1100, 780);
    dlg.show();
    QApplication::processEvents();

    auto* view = dlg.findChild<DrawingSheetWidget*>(QStringLiteral("drawingSheetView"));
    auto* tool = dlg.findChild<QComboBox*>(QStringLiteral("drawingTool"));
    auto* undoBtn = dlg.findChild<QPushButton*>(QStringLiteral("drawingUndo"));
    auto* redoBtn = dlg.findChild<QPushButton*>(QStringLiteral("drawingRedo"));
    auto* hideBtn = dlg.findChild<QPushButton*>(QStringLiteral("drawingHide"));
    check(view && tool && undoBtn && redoBtn && hideBtn, "GUI: pafta gorunumu ve duzenleme araclari mevcut");
    check(dlg.currentDocument() && dlg.currentSheet() && dlg.currentSheet()->layout.ok, "GUI: tek pafta seti yuklendi");
    if (!view || !tool || !undoBtn || !redoBtn || !hideBtn) return 1;

    const DrawingSheet* sheet = dlg.currentSheet();
    const SheetFormat fmt = sheet->format;
    const Map m = mapper(*view, fmt);
    const QImage screen0 = renderSheetImage(*sheet, 1400);

    // 1) Cizgi aracini sec, bos alana cizgi ciz -> sekil eklenir, ekran/PDF'te gorunur.
    tool->setCurrentIndex(1);
    check(view->tool() == DrawingSheetWidget::EditTool::Line, "GUI: arac secimi Cizgi");
    drag(*view, m.toWidget({30.0, 170.0}), m.toWidget({150.0, 170.0}));
    const DrawingSheet* afterLine = dlg.currentSheet();
    check(afterLine->shapes.size() == 1 && afterLine->shapes.front().kind == ShapeKind::Line,
          "GUI: fare ile cizgi cizildi (2B sekil)");
    const QImage screen1 = renderSheetImage(*afterLine, 1400);
    check(diffCount(screen0, screen1) > 40, "GUI: ekran renderinda yeni cizgi gorunuyor");

    // 2) Geri al / yinele dugmeleri islevsel.
    undoBtn->click();
    check(dlg.currentSheet()->shapes.empty(), "GUI: Geri al -> cizgi kaldirildi");
    redoBtn->click();
    check(dlg.currentSheet()->shapes.size() == 1, "GUI: Yinele -> cizgi geri geldi");

    // 3) Sec ve tasi (sekil).
    tool->setCurrentIndex(0);
    const Vec2 mid{90.0, 170.0};
    click(*view, m.toWidget(mid));
    check(view->selection().kind == PaperHitKind::Shape, "GUI: sekil tiklamayla secildi");
    drag(*view, m.toWidget(mid), m.toWidget({110.0, 180.0}));
    check(std::fabs(dlg.currentSheet()->shapes.front().a.x - 50.0) < 1.0 &&
              std::fabs(dlg.currentSheet()->shapes.front().a.y - 180.0) < 1.0,
          "GUI: sekil fare ile tasindi");

    // 4) Olcu tasima: deger DEGISMEZ.
    const DrawingSheet* sh = dlg.currentSheet();
    const Dimension2D* dim = nullptr;
    for (const Dimension2D& d : sh->dimensions)
        if (d.featureKey.empty()) dim = &d;
    check(dim != nullptr, "GUI: geometri olcusu var");
    if (dim) {
        const DimensionGeometry g = resolveDimension(*sh, *dim);
        click(*view, m.toWidget(g.textCentre));
        check(view->selection().kind == PaperHitKind::Dimension, "GUI: olcu tiklamayla secildi");
        const ElementId dimId = view->selection().id;
        const Dimension2D* sel = nullptr;
        for (const Dimension2D& d : dlg.currentSheet()->dimensions)
            if (d.id == dimId) sel = &d;
        const double value0 = sel ? sel->value : 0.0;
        const double off0 = sel ? sel->offset : 0.0;
        // Olcu cizgisini gorunusten DISA dogru surukle (offset buyur; iceri dogru kucultup 0.5'te kilitlenebilir).
        const DrawingView* dv = dlg.currentSheet()->findView(sel ? sel->viewId : 0);
        Vec2 outward{0.0, -1.0};
        if (dv) {
            const Vec2 c = dv->paperBounds().centre();
            const double dx = g.textCentre.x - c.x, dy = g.textCentre.y - c.y;
            const double l = std::hypot(dx, dy);
            if (l > 1e-6) outward = {dx / l, dy / l};
        }
        drag(*view, m.toWidget(g.textCentre), m.toWidget({g.textCentre.x + outward.x * 8.0,
                                                          g.textCentre.y + outward.y * 8.0}));
        const Dimension2D* moved = nullptr;
        for (const Dimension2D& d : dlg.currentSheet()->dimensions)
            if (d.id == dimId) moved = &d;
        check(moved && std::fabs(moved->offset - off0) > 0.5, "GUI: olcu cizgisi tasindi (offset degisti)");
        check(moved && std::fabs(moved->value - value0) < 1e-9 && std::fabs(moved->measured() - moved->value) < 1e-6,
              "GUI: olcu DEGERI degismedi (measured == value)");
    }

    // 5) PDF paritesi: ayni plan vektor PDF'te de cizilir (raster denetimi).
    {
        QTemporaryDir dir;
        DrawingDocument baseline;
        DrawingSheet clean = *dlg.currentSheet();
        clean.shapes.clear();
        baseline.addSheet(clean);
        const QString pdf0 = dir.filePath("base.pdf");
        const QString pdf1 = dir.filePath("edited.pdf");
        DrawingDocument edited;
        edited.addSheet(*dlg.currentSheet());
        check(edited.sheets().front().shapes.size() == 1, "GUI: PDF'e giren belgede 2B sekil var");
        QString err;
        check(exportDrawingSetPdf(baseline, pdf0, &err) && exportDrawingSetPdf(edited, pdf1, &err),
              "GUI: iki PDF yazildi");
        QFile a(pdf0), b(pdf1);
        if (a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly)) {
            const QByteArray ba = a.readAll(), bb = b.readAll();
            check(bb.size() > ba.size() && !bb.contains("/Subtype /Image"), "GUI: PDF vektor ve duzenlemeyi iceriyor");
        }
        auto raster = [&](const QString& pdf, const QString& prefix) {
            QProcess p;
            p.start("pdftoppm", {"-r", "300", "-png", "-f", "1", "-l", "1", pdf, dir.filePath(prefix)});
            p.waitForFinished(60000);
            const QString img = dir.filePath(prefix + "-1.png");
            return QImage(img);
        };
        const QImage i0 = raster(pdf0, "p0");
        const QImage i1 = raster(pdf1, "p1");
        check(inkCount(i0) > 0 && inkCount(i1) > 0 && diffCount(i0, i1) > 400,
              "GUI: PDF rasterinda eklenen cizgi baskiya donusuyor (parite)");
    }

    // 6) Gizleme: zorunlu imalat olcusu -> TASLAK/INCOMPLETE (sahte READY yok).
    {
        const DrawingSheet* cur = dlg.currentSheet();
        const Dimension2D* fab = nullptr;
        for (const Dimension2D& d : cur->dimensions)
            if (!d.featureKey.empty()) fab = &d;
        check(fab != nullptr, "GUI: imalat kaynakli olcu var");
        if (fab) {
            const DimensionGeometry g = resolveDimension(*cur, *fab);
            click(*view, m.toWidget(g.textCentre));
            check(view->selection().kind == PaperHitKind::Dimension, "GUI: imalat olcusu secildi");
            hideBtn->click();
            const DrawingSheet* after = dlg.currentSheet();
            bool item = false;
            for (const ChecklistItem& c : after->fabrication.checklist)
                if (c.key == "ANNOTATIONS" && c.state == ChecklistState::Unresolved) item = true;
            check(after->fabrication.draft && item, "GUI: gizlenen imalat olcusu cizimi TASLAK yapti");
            undoBtn->click();
            check(!dlg.currentSheet()->fabrication.draft, "GUI: geri al -> TASLAK kalkti");
        }
    }

    check(f.doc.revision() == rev0, "GUI: tum duzenlemeler boyunca MODEL DEGISMEDI");
    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
