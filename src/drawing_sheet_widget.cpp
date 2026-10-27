#include "model_maker/drawing_sheet_widget.hpp"

#include "model_maker/drawing_builder.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/utf8.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPageSize>
#include <QPaintEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>

namespace mm::drawing {
namespace {

constexpr double kPi = 3.14159265358979323846;

QString qs(const std::string& s) { return QString::fromStdWString(text::fromUtf8(s)); }

QPointF pt(const Vec2& v) { return QPointF(v.x, v.y); }

// Metin: konum (sayfa mm, y yukari), BUYUK HARF/RAKAM YUKSEKLIGI mm (heightMm), hizalama, aci (derece, saat yonu tersi).
// em = capHeight / 0.70 (tipik govde orani): yerlesim motorunun metin kutusu (textWidthEstimate) ayni varsayimla hesaplanir.
constexpr double kCapRatio = 0.70;

void drawText(QPainter& p, const QPointF& at, const QString& t, double heightMm, Qt::Alignment al,
              double angleDeg = 0.0, bool bold = false) {
    p.save();
    p.translate(at);
    if (angleDeg != 0.0) p.rotate(angleDeg);
    const double em = heightMm / kCapRatio;
    p.scale(em / 40.0, -em / 40.0);
    QFont f = p.font();
    f.setPixelSize(40);
    f.setBold(bold);
    p.setFont(f);
    const double big = 4000.0;
    const double x = (al & Qt::AlignHCenter) ? -big / 2 : (al & Qt::AlignRight) ? -big : 0.0;
    const double y = (al & Qt::AlignVCenter) ? -big / 2 : (al & Qt::AlignBottom) ? -big : 0.0;
    p.drawText(QRectF(x, y, big, big), static_cast<int>(al), t);
    p.restore();
}

}  // namespace

// Gercek Qt metin genisligi (kagit mm) -- yerlesim tahmininin muhafazakarligini dogrulayan test icin disa acik.
double measureTextWidthMm(const QString& t, double capHeightMm, bool bold) {
    QFont f;
    f.setPixelSize(40);
    f.setBold(bold);
    return QFontMetricsF(f).horizontalAdvance(t) * (capHeightMm / kCapRatio) / 40.0;
}

namespace {

QString valueText(double v) { return QString::fromStdString(dimensionText(v)); }

void tick(QPainter& p, const QPointF& at) {
    p.drawLine(QPointF(at.x() - 1.2, at.y() - 1.2), QPointF(at.x() + 1.2, at.y() + 1.2));
}

void drawTitleBlock(QPainter& p, const DrawingSheet& s) {
    const SheetFormat& f = s.format;
    const QRectF box(f.width - f.margin - f.titleWidth, f.margin, f.titleWidth, f.titleHeight);
    p.setPen(QPen(Qt::black, lineWeightMm::titleOuter));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    p.setPen(QPen(Qt::black, lineWeightMm::titleInner));
    const double rowH = box.height() / 4.0;
    for (int i = 1; i < 4; ++i)
        p.drawLine(QPointF(box.left(), box.top() + rowH * i), QPointF(box.right(), box.top() + rowH * i));
    const double third = box.width() / 3.0;
    for (int row : {0, 1})  // alt iki satir 3 kolon (y yukari: row 0 = en alt)
        for (int c = 1; c < 3; ++c)
            p.drawLine(QPointF(box.left() + third * c, box.top() + rowH * row),
                       QPointF(box.left() + third * c, box.top() + rowH * (row + 1)));
    const TitleBlock& t = s.titleBlock;
    const auto cell = [&](int row, int col, int cols, const QString& label, const QString& value, double h) {
        const double w = box.width() / cols;
        const QPointF tl(box.left() + w * col + 1.5, box.top() + rowH * (row + 1) - 1.0);
        p.setPen(QPen(QColor(90, 90, 90), 0.1));
        drawText(p, tl, label, 2.0, Qt::AlignLeft | Qt::AlignTop);
        p.setPen(QPen(Qt::black, 0.1));
        drawText(p, QPointF(box.left() + w * col + 1.5, box.top() + rowH * row + rowH * 0.35), value, h,
                 Qt::AlignLeft | Qt::AlignVCenter);
    };
    cell(3, 0, 1, QStringLiteral("PROJE"), qs(t.project), 4.0);
    cell(2, 0, 1, QStringLiteral("BA\u015eLIK"), qs(t.title), 4.5);
    cell(1, 0, 3, QStringLiteral("\u00c7\u0130Z\u0130M NO"), qs(t.drawingNumber), 4.5);
    cell(1, 1, 3, QStringLiteral("L\u00c7EK"), qs(t.scaleText), 4.5);
    cell(1, 2, 3, QStringLiteral("REV"), qs(t.revision), 4.0);
    cell(0, 0, 3, QStringLiteral("\u00c7\u0130ZEN"), qs(t.drawnBy), 3.5);
    cell(0, 1, 3, QStringLiteral("TAR\u0130H"), qs(t.date), 3.5);
    cell(0, 2, 3, QStringLiteral("KA\u011eIT"), QString::fromLatin1(sheetSizeName(f.size)), 3.5);
}

void drawDimension(QPainter& p, const DrawingSheet& s, const Dimension2D& d) {
    // Geometri + metin kutusu yerlesim motorunun ORTAK cozumleyicisinden: onizleme == PDF == olculen plan.
    const DimensionGeometry g = resolveDimension(s, d);
    if (!g.valid) return;
    p.setPen(QPen(QColor(0, 90, 160), lineWeightMm::dimension));
    for (const LineSeg& l : g.lines) p.drawLine(pt(l.a), pt(l.b));
    p.drawLine(pt(g.tick1.a), pt(g.tick1.b));
    p.drawLine(pt(g.tick2.a), pt(g.tick2.b));
    drawText(p, pt(g.textCentre), valueText(d.value), g.textHeight, Qt::AlignCenter, g.textAngleDeg);
}

void drawMark(QPainter& p, const DrawingSheet& s, const MarkAnnotation& m) {
    const DrawingView* v = s.findView(m.viewId);
    if (!v) return;
    const QString label = qs(m.text);
    const double h = m.isAssembly ? 5.0 : 3.5;
    const Bounds2 bb = markBox(m);   // yerlesim motoruyla AYNI kutu
    const QRectF box(bb.min.x, bb.min.y, bb.width(), bb.height());
    const QPointF anchor = pt(v->toPaper(m.anchor));
    const QColor col = m.isAssembly ? QColor(170, 40, 40) : QColor(30, 110, 50);
    p.setPen(QPen(col, lineWeightMm::mark));
    p.drawLine(anchor, box.center());
    p.setBrush(col);
    p.drawEllipse(anchor, 0.7, 0.7);
    p.setBrush(Qt::white);
    p.setPen(QPen(col, m.isAssembly ? lineWeightMm::markAssembly : lineWeightMm::mark));
    p.drawRoundedRect(box, 1.0, 1.0);
    drawText(p, box.center(), label, h, Qt::AlignCenter, 0.0, m.isAssembly);
}

void drawTextAnnotation(QPainter& p, const DrawingSheet& s, const TextAnnotation& t) {
    const TextGeometry g = resolveText(s, t);
    QColor col = QColor(40, 40, 40);
    if (t.kind == TextKind::StatusStamp) col = s.fabrication.draft ? QColor(190, 20, 20) : QColor(30, 110, 50);
    else if (t.kind == TextKind::HoleCallout) col = QColor(120, 60, 0);
    else if (t.kind == TextKind::ViewTitle) col = Qt::black;
    if (g.hasLeader) {
        p.setPen(QPen(col, lineWeightMm::mark));
        p.drawLine(pt(g.leader.a), pt(g.leader.b));
        p.setBrush(col);
        p.drawEllipse(pt(g.leader.a), 0.5, 0.5);
    }
    if (t.kind == TextKind::StatusStamp && s.fabrication.draft) {
        p.setPen(QPen(col, lineWeightMm::markAssembly));
        p.setBrush(Qt::NoBrush);
        const Bounds2 ab = g.box.aabb();
        p.drawRect(QRectF(ab.min.x, ab.min.y, ab.width(), ab.height()));
    }
    p.setPen(QPen(col, 0.1));
    for (std::size_t i = 0; i < t.lines.size() && i < g.lineCentres.size(); ++i)
        drawText(p, pt(g.lineCentres[i]), qs(t.lines[i]), t.height, Qt::AlignLeft | Qt::AlignVCenter, 0.0,
                 t.bold || t.kind == TextKind::ViewTitle);
}

} // namespace

void paintSheet(QPainter& painter, const DrawingSheet& sheet, const QRectF& target) {
    const SheetFormat& f = sheet.format;
    const double s = std::min(target.width() / f.width, target.height() / f.height);
    if (s <= 0.0) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(target.center().x() - f.width * s / 2.0, target.center().y() + f.height * s / 2.0);
    painter.scale(s, -s);

    painter.fillRect(QRectF(0, 0, f.width, f.height), Qt::white);
    painter.setPen(QPen(Qt::black, lineWeightMm::frame));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(f.margin, f.margin, f.width - 2 * f.margin, f.height - 2 * f.margin));
    drawTitleBlock(painter, sheet);

    for (const DrawingView& v : sheet.views) {
        for (const Segment2& seg : v.segments) {
            if (seg.style == LineStyle::Visible) {
                painter.setPen(QPen(Qt::black, lineWeightMm::visible, Qt::SolidLine, Qt::RoundCap));
            } else {
                QPen pen(QColor(110, 110, 110), lineWeightMm::hidden, Qt::DashLine);
                pen.setDashPattern({10.0, 6.0});
                painter.setPen(pen);
            }
            painter.drawLine(pt(v.toPaper(seg.a)), pt(v.toPaper(seg.b)));
        }
        for (const Circle2& c : v.circles) {
            const QPointF ctr = pt(v.toPaper(c.center));
            const double rp = c.radius * v.scale;
            const double arm = std::max(rp, 0.4) + 1.2;
            painter.setPen(QPen(QColor(120, 120, 120), lineWeightMm::centre, Qt::DashDotLine));
            painter.drawLine(QPointF(ctr.x() - arm, ctr.y()), QPointF(ctr.x() + arm, ctr.y()));
            painter.drawLine(QPointF(ctr.x(), ctr.y() - arm), QPointF(ctr.x(), ctr.y() + arm));
            painter.setPen(QPen(c.style == LineStyle::Visible ? QColor(Qt::black) : QColor(110, 110, 110),
                                lineWeightMm::circle));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(ctr, rp, rp);
            if (!c.label.empty()) {
                painter.setPen(QPen(QColor(60, 60, 60), 0.1));
                drawText(painter, QPointF(ctr.x() + arm, ctr.y() + arm), qs(c.label), 2.6,
                         Qt::AlignLeft | Qt::AlignBottom);
            }
        }
    }
    for (const Dimension2D& d : sheet.dimensions) {
        if (!d.visible) continue;
        drawDimension(painter, sheet, d);
    }
    for (const MarkAnnotation& m : sheet.marks) {
        if (!m.visible) continue;
        drawMark(painter, sheet, m);
    }
    for (const TextAnnotation& t : sheet.texts) {
        if (!t.visible) continue;
        drawTextAnnotation(painter, sheet, t);
    }
    // P2B: kullanici 2B cizimleri (ayni plan PDF ve ekranda; vektor).
    for (const PaperShape& sh : sheet.shapes) {
        if (!sh.visible) continue;
        if (sh.kind == ShapeKind::Circle) {
            const QPointF c = pt(sh.center);
            painter.setPen(QPen(QColor(120, 120, 120), lineWeightMm::centre, Qt::DashDotLine));
            painter.drawLine(QPointF(c.x() - sh.radius - 1.2, c.y()), QPointF(c.x() + sh.radius + 1.2, c.y()));
            painter.drawLine(QPointF(c.x(), c.y() - sh.radius - 1.2), QPointF(c.x(), c.y() + sh.radius + 1.2));
            painter.setPen(QPen(Qt::black, lineWeightMm::circle));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(c, sh.radius, sh.radius);
        } else {
            QPen pen(Qt::black, sh.kind == ShapeKind::Centerline ? lineWeightMm::centre : lineWeightMm::visible);
            if (sh.kind == ShapeKind::Centerline) pen.setStyle(Qt::DashDotLine);
            if (sh.style == LineStyle::Hidden) {
                pen.setColor(QColor(110, 110, 110));
                pen.setStyle(Qt::DashLine);
            }
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(pt(sh.a), pt(sh.b));
        }
    }
    painter.restore();
}

QImage renderSheetImage(const DrawingSheet& sheet, int widthPixels) {
    const int h = static_cast<int>(std::lround(widthPixels * sheet.format.height / sheet.format.width));
    QImage img(widthPixels, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    paintSheet(p, sheet, QRectF(0, 0, widthPixels, h));
    p.end();
    return img;
}

bool exportSheetPdf(const DrawingSheet& sheet, const QString& path, QString* error) {
    QPdfWriter pdf(path);
    pdf.setResolution(300);
    // Kisa x uzun kenar (A3 = 297 x 420) + Landscape: sayfa yatay, kagit boyutu taninir.
    pdf.setPageLayout(QPageLayout(
        QPageSize(QSizeF(std::min(sheet.format.width, sheet.format.height),
                         std::max(sheet.format.width, sheet.format.height)),
                  QPageSize::Millimeter),
        QPageLayout::Landscape, QMarginsF(0, 0, 0, 0)));
    pdf.setTitle(qs(sheet.name));
    pdf.setCreator(QStringLiteral("Model Maker"));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error) *error = QStringLiteral("PDF dosyasi acilamadi: ") + path;
        return false;
    }
    paintSheet(p, sheet, QRectF(0, 0, pdf.width(), pdf.height()));
    p.end();
    return true;
}

bool exportDrawingSetPdf(const DrawingDocument& document, const QString& path, QString* error) {
    if (document.sheets().empty()) {
        if (error) *error = QStringLiteral("Cizim seti bos");
        return false;
    }
    QPdfWriter pdf(path);
    pdf.setResolution(300);
    pdf.setCreator(QStringLiteral("Model Maker"));
    pdf.setTitle(qs(document.sheets().front().name));
    QPainter p;
    bool begun = false;
    for (std::size_t i = 0; i < document.sheets().size(); ++i) {
        const DrawingSheet& sheet = document.sheets()[i];
        const double w = sheet.format.width;
        const double h = sheet.format.height;
        const QPageLayout layout(
            QPageSize(QSizeF(std::min(w, h), std::max(w, h)), QPageSize::Millimeter),
            QPageLayout::Landscape, QMarginsF(0, 0, 0, 0));
        if (i > 0) {
            pdf.setPageLayout(layout);
            pdf.newPage();
        } else {
            pdf.setPageLayout(layout);
            if (!p.begin(&pdf)) {
                if (error) *error = QStringLiteral("PDF dosyasi acilamadi: ") + path;
                return false;
            }
            begun = true;
        }
        paintSheet(p, sheet, QRectF(0, 0, pdf.width(), pdf.height()));
    }
    if (begun) p.end();
    return true;
}

// ---------------------------------------------------------------------------------------

DrawingSheetWidget::DrawingSheetWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(520, 380);
    setObjectName(QStringLiteral("drawingSheetView"));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(70, 74, 82));
    setPalette(pal);
}

void DrawingSheetWidget::setSheet(const DrawingSheet* sheet) {
    sheet_ = sheet;
    update();
}

void DrawingSheetWidget::setEditor(PaperEditHistory* history) {
    history_ = history;
    sheet_ = history ? &history->sheet() : nullptr;
    selection_ = PaperHit{};
    dragging_ = false;
    drawing_ = false;
    update();
}

Vec2 DrawingSheetWidget::paperAt(const QPointF& pos) const {
    if (!sheet_) return {};
    const QRectF t = targetRect();
    const SheetFormat& f = sheet_->format;
    const double s = std::min(t.width() / f.width, t.height() / f.height);
    if (s <= 1e-9) return {};
    const double ox = t.center().x() - f.width * s / 2.0;
    const double oy = t.center().y() + f.height * s / 2.0;
    return {(pos.x() - ox) / s, (oy - pos.y()) / s};
}

void DrawingSheetWidget::undoEdit() {
    if (history_ && history_->undo()) {
        selection_ = PaperHit{};
        update();
    }
}

void DrawingSheetWidget::redoEdit() {
    if (history_ && history_->redo()) {
        selection_ = PaperHit{};
        update();
    }
}

bool DrawingSheetWidget::hideSelection() {
    if (!history_ || !selection_.valid()) return false;
    history_->begin();
    const bool ok = setPaperElementVisible(history_->sheet(), selection_, false);
    if (ok) evaluateAnnotationStatus(history_->sheet());
    history_->commit();
    selection_ = PaperHit{};
    update();
    return ok;
}

void DrawingSheetWidget::fitToWindow() {
    zoom_ = 1.0;
    pan_ = {};
    fitted_ = true;
    update();
}

QRectF DrawingSheetWidget::targetRect() const {
    const QRectF base = QRectF(rect()).adjusted(8, 8, -8, -8);
    const QPointF c = base.center() + pan_;
    const QSizeF sz(base.width() * zoom_, base.height() * zoom_);
    return QRectF(c.x() - sz.width() / 2, c.y() - sz.height() / 2, sz.width(), sz.height());
}

void DrawingSheetWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (!sheet_) {
        p.setPen(QColor(200, 200, 200));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("\u00c7izim yok"));
        return;
    }
    paintSheet(p, *sheet_, targetRect());
}

void DrawingSheetWidget::wheelEvent(QWheelEvent* e) {
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    const double nz = std::clamp(zoom_ * std::pow(1.15, steps), 0.2, 40.0);
    const double k = nz / zoom_;
    const QPointF q = e->position();
    const QRectF base = QRectF(rect()).adjusted(8, 8, -8, -8);
    const QPointF c = base.center() + pan_;
    pan_ = (q + (c - q) * k) - base.center();
    zoom_ = nz;
    fitted_ = false;
    update();
}

void DrawingSheetWidget::mousePressEvent(QMouseEvent* e) {
    lastPos_ = e->pos();
    if (!history_ || e->button() != Qt::LeftButton) return;
    const Vec2 p = paperAt(e->pos());
    if (tool_ == EditTool::Select) {
        const QRectF t = targetRect();
        const double ppm = std::min(t.width() / sheet_->format.width, t.height() / sheet_->format.height);
        const double tolMm = ppm > 1e-9 ? 6.0 / ppm : 1.5;
        selection_ = hitTestPaper(*sheet_, p, tolMm);
        if (selection_.valid()) {
            history_->begin();
            dragging_ = true;
            lastPaper_ = p;
        }
        update();
        return;
    }
    drawing_ = true;
    drawStart_ = p;
}

void DrawingSheetWidget::mouseDoubleClickEvent(QMouseEvent*) { fitToWindow(); }

void DrawingSheetWidget::mouseMoveEvent(QMouseEvent* e) {
    if (history_ && dragging_) {
        const Vec2 p = paperAt(e->pos());
        if (movePaperElement(history_->sheet(), selection_, {p.x - lastPaper_.x, p.y - lastPaper_.y})) {
            lastPaper_ = p;
            update();
        }
        return;
    }
    if (history_ && drawing_) {
        update();
        return;
    }
    if (e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) {
        pan_ += QPointF(e->pos() - lastPos_);
        lastPos_ = e->pos();
        fitted_ = false;
        update();
    }
}

void DrawingSheetWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!history_) return;
    if (dragging_) {
        history_->commit();
        dragging_ = false;
        update();
        return;
    }
    if (drawing_) {
        const Vec2 end = paperAt(e->pos());
        const double dx = end.x - drawStart_.x, dy = end.y - drawStart_.y;
        const double dist = std::hypot(dx, dy);
        drawing_ = false;
        if (dist >= 1.0) {
            history_->begin();
            const ShapeKind kind = tool_ == EditTool::Circle ? ShapeKind::Circle
                                    : (tool_ == EditTool::Centerline ? ShapeKind::Centerline : ShapeKind::Line);
            addPaperShape(history_->sheet(), kind, drawStart_, end, tool_ == EditTool::Circle ? dist : 0.0);
            history_->commit();
            update();
        }
    }
}

void DrawingSheetWidget::keyPressEvent(QKeyEvent* e) {
    if (history_ && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        hideSelection();
        return;
    }
    QWidget::keyPressEvent(e);
}

void DrawingSheetWidget::resizeEvent(QResizeEvent*) { update(); }

// ---------------------------------------------------------------------------------------

DrawingPreviewDialog::DrawingPreviewDialog(std::vector<SubjectChoice> subjects, int defaultSubject,
                                           Builder builder, QWidget* parent, SetBuilder setBuilder)
    : QDialog(parent), subjects_(std::move(subjects)), builder_(std::move(builder)), setBuilder_(std::move(setBuilder)) {
    setWindowTitle(QStringLiteral("Se\u00e7imden \u00c7izim"));
    setObjectName(QStringLiteral("drawingPreviewDialog"));
    resize(1100, 780);
    auto* root = new QVBoxLayout(this);

    auto* opts = new QHBoxLayout;
    subjectCombo_ = new QComboBox(this);
    subjectCombo_->setObjectName(QStringLiteral("drawingSubject"));
    for (const auto& s : subjects_) subjectCombo_->addItem(s.label);
    if (defaultSubject >= 0 && defaultSubject < subjectCombo_->count()) subjectCombo_->setCurrentIndex(defaultSubject);
    sizeCombo_ = new QComboBox(this);
    sizeCombo_->setObjectName(QStringLiteral("drawingSize"));
    for (const char* n : {"A4", "A3", "A2", "A1", "A0"}) sizeCombo_->addItem(QString::fromLatin1(n));
    sizeCombo_->setCurrentText(QStringLiteral("A3"));
    if (setBuilder_) {
        // P2C varsayilanlari: otomatik kagit + sigmazsa bagli coklu pafta.
        autoSizeCheck_ = new QCheckBox(QStringLiteral("Otomatik kagit (A3-A0)"), this);
        autoSizeCheck_->setObjectName(QStringLiteral("drawingAutoSize"));
        autoSizeCheck_->setChecked(true);
        sizeCombo_->setEnabled(false);
        multiSheetCheck_ = new QCheckBox(QStringLiteral("Sigmazsa bagli paftalar"), this);
        multiSheetCheck_->setObjectName(QStringLiteral("drawingMultiSheet"));
        multiSheetCheck_->setChecked(true);
    }
    projectionCombo_ = new QComboBox(this);
    projectionCombo_->setObjectName(QStringLiteral("drawingProjection"));
    projectionCombo_->addItem(QStringLiteral("3. a\u00e7\u0131 (\u00dcst \u00fcstte, Yan sa\u011fda)"));
    projectionCombo_->addItem(QStringLiteral("1. a\u00e7\u0131 (\u00dcst altta, Yan solda)"));
    frontCheck_ = new QCheckBox(QStringLiteral("\u00d6n"), this);
    topCheck_ = new QCheckBox(QStringLiteral("\u00dcst"), this);
    sideCheck_ = new QCheckBox(QStringLiteral("Yan"), this);
    for (QCheckBox* c : {frontCheck_, topCheck_, sideCheck_}) c->setChecked(true);
    frontCheck_->setObjectName(QStringLiteral("drawingFront"));
    topCheck_->setObjectName(QStringLiteral("drawingTop"));
    sideCheck_->setObjectName(QStringLiteral("drawingSide"));
    hiddenCheck_ = new QCheckBox(QStringLiteral("Gizli \u00e7izgiler (deneysel)"), this);
    hiddenCheck_->setObjectName(QStringLiteral("drawingHidden"));
    opts->addWidget(new QLabel(QStringLiteral("Konu:"), this));
    opts->addWidget(subjectCombo_, 1);
    opts->addWidget(new QLabel(QStringLiteral("Ka\u011f\u0131t:"), this));
    opts->addWidget(sizeCombo_);
    if (setBuilder_) {
        opts->addWidget(autoSizeCheck_);
        opts->addWidget(multiSheetCheck_);
        sheetCombo_ = new QComboBox(this);
        sheetCombo_->setObjectName(QStringLiteral("drawingSheetPage"));
        sheetCombo_->setMinimumWidth(180);
        opts->addWidget(sheetCombo_);
        toolCombo_ = new QComboBox(this);
        toolCombo_->setObjectName(QStringLiteral("drawingTool"));
        toolCombo_->addItems({QStringLiteral("Sec"), QStringLiteral("Cizgi"), QStringLiteral("Daire"),
                              QStringLiteral("Merkez cizgisi")});
        opts->addWidget(toolCombo_);
    }
    opts->addWidget(projectionCombo_);
    opts->addWidget(frontCheck_);
    opts->addWidget(topCheck_);
    opts->addWidget(sideCheck_);
    opts->addWidget(hiddenCheck_);
    root->addLayout(opts);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("drawingStatus"));
    root->addWidget(status_);

    view_ = new DrawingSheetWidget(this);
    root->addWidget(view_, 1);

    auto* buttons = new QHBoxLayout;
    auto* fit = new QPushButton(QStringLiteral("S\u0131\u011fd\u0131r"), this);
    auto* pdf = new QPushButton(QStringLiteral("PDF'e aktar..."), this);
    pdf->setObjectName(QStringLiteral("drawingPdf"));
    auto* json = new QPushButton(QStringLiteral("JSON kaydet..."), this);
    json->setObjectName(QStringLiteral("drawingJson"));
    auto* close = new QPushButton(QStringLiteral("Kapat"), this);
    buttons->addWidget(fit);
    if (setBuilder_) {
        auto* undoBtn = new QPushButton(QStringLiteral("Geri al"), this);
        undoBtn->setObjectName(QStringLiteral("drawingUndo"));
        auto* redoBtn = new QPushButton(QStringLiteral("Yinele"), this);
        redoBtn->setObjectName(QStringLiteral("drawingRedo"));
        auto* hideBtn = new QPushButton(QStringLiteral("Gizle"), this);
        hideBtn->setObjectName(QStringLiteral("drawingHide"));
        buttons->addWidget(undoBtn);
        buttons->addWidget(redoBtn);
        buttons->addWidget(hideBtn);
        connect(undoBtn, &QPushButton::clicked, this, [this]() { view_->undoEdit(); });
        connect(redoBtn, &QPushButton::clicked, this, [this]() { view_->redoEdit(); });
        connect(hideBtn, &QPushButton::clicked, this, [this]() { view_->hideSelection(); });
    }
    buttons->addStretch(1);
    buttons->addWidget(pdf);
    buttons->addWidget(json);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(fit, &QPushButton::clicked, view_, &DrawingSheetWidget::fitToWindow);
    connect(pdf, &QPushButton::clicked, this, [this]() { onExportPdf(); });
    connect(json, &QPushButton::clicked, this, [this]() { onSaveJson(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    const auto changed = [this]() { rebuild(); };
    connect(subjectCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(projectionCombo_, &QComboBox::currentIndexChanged, this, changed);
    for (QCheckBox* c : {frontCheck_, topCheck_, sideCheck_, hiddenCheck_})
        connect(c, &QCheckBox::toggled, this, changed);
    if (setBuilder_) {
        connect(autoSizeCheck_, &QCheckBox::toggled, this, [this](bool on) {
            sizeCombo_->setEnabled(!on);
            rebuild();
        });
        connect(multiSheetCheck_, &QCheckBox::toggled, this, changed);
        connect(sheetCombo_, &QComboBox::currentIndexChanged, this, [this](int i) { showSheet(i); });
        connect(toolCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
            view_->setTool(i == 1 ? DrawingSheetWidget::EditTool::Line
                                  : (i == 2 ? DrawingSheetWidget::EditTool::Circle
                                            : (i == 3 ? DrawingSheetWidget::EditTool::Centerline
                                                      : DrawingSheetWidget::EditTool::Select)));
        });
    }
    rebuild();
}

void DrawingPreviewDialog::setRebuildConfirmation(std::function<bool(const QString&)> confirm) {
    confirmDiscard_ = std::move(confirm);
}

std::vector<std::pair<int, DrawingSheet>> DrawingPreviewDialog::collectEditedSheets() const {
    std::vector<std::pair<int, DrawingSheet>> out;
    if (!haveDoc_) return out;
    for (std::size_t i = 0; i < doc_.sheets().size(); ++i) {
        const DrawingSheet& cur =
            (edit_ && static_cast<int>(i) == currentSheet_) ? edit_->sheet() : doc_.sheets()[i];
        const DrawingSheet& base = i < baseline_.size() ? baseline_[i] : cur;
        DrawingDocument a;
        DrawingDocument b;
        a.addSheet(base);
        b.addSheet(cur);
        if (a.serialize() != b.serialize()) out.emplace_back(static_cast<int>(i), cur);
    }
    return out;
}

// P2D: duzenlemeleri yeniden uretilen paftalara uygular. Eslesme PAFTA INDEKSI ile degil, KAYNAK
// OGEDEN (gorunus/olcu/mark/metin anahtari) yapilir: sigdirma bir gorunusu baska paftaya tasisa da
// olcu yerlesimi kaybolmaz. Kullanici 2B sekilleri pafta duzenindedir: indeks varsa o paftaya,
// sayfa sayisi azaldiysa ana paftaya eklenir (kimlik cakismasi yeniden atanir).
int DrawingPreviewDialog::mergePreservedEdits(const std::vector<std::pair<int, DrawingSheet>>& edits,
                                              DrawingSetResult& r) {
    if (edits.empty() || r.document.sheets().empty()) return 0;
    const std::size_t n = r.document.sheets().size();
    int applied = 0;
    for (DrawingSheet& sheet : r.document.sheets()) {
        std::vector<DrawingSheet> editedOnly;
        for (const auto& e : edits) editedOnly.push_back(e.second);
        const PaperEditMerge m = mergePaperEdits(editedOnly, sheet, false);
        applied = std::max(applied, m.sheets);
    }
    std::vector<std::vector<PaperShape>> shapesFor(n);
    for (const auto& [idx, edited] : edits) {
        const std::size_t target =
            (idx >= 0 && static_cast<std::size_t>(idx) < n) ? static_cast<std::size_t>(idx) : 0;
        shapesFor[target].insert(shapesFor[target].end(), edited.shapes.begin(), edited.shapes.end());
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (shapesFor[i].empty()) continue;
        appendPaperShapes(r.document.sheets()[i], shapesFor[i]);
    }
    for (DrawingSheet& sheet : r.document.sheets()) refreshSheetQuality(sheet);
    bool allOk = true;
    bool draft = false;
    for (const DrawingSheet& s : r.document.sheets()) {
        allOk = allOk && s.layout.ok && measureOverlaps(s).total() == 0;
        draft = draft || s.fabrication.draft;
    }
    r.layoutValid = allOk;
    r.ready = allOk && !draft;
    return applied;
}

void DrawingPreviewDialog::rebuild() {
    // P2D: konu degisikligi kaynak eslesmesini bozar -> duzenlemeler tasinamaz; ACIK onay istenir.
    const int wantedIdx = subjectCombo_ ? subjectCombo_->currentIndex() : -1;
    const bool sameSubject = builtSubjectIndex_ < 0 || wantedIdx == builtSubjectIndex_;
    std::vector<std::pair<int, DrawingSheet>> edits;
    if (setBuilder_ && sameSubject) edits = collectEditedSheets();
    if (setBuilder_ && !sameSubject && haveDoc_ && !collectEditedSheets().empty()) {
        const QString msg = QStringLiteral(
            "Konu degisiyor: onceki paftadaki duzenlemeler yeni cizime tasinamaz. Devam edilsin mi?");
        const bool go = confirmDiscard_
                            ? confirmDiscard_(msg)
                            : (QMessageBox::question(this, QStringLiteral("Pafta duzenlemeleri"), msg,
                                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
                               QMessageBox::Yes);
        if (!go) {   // reddedildi: hicbir sey kaybolmaz, konu eski haline doner
            if (builtSubjectIndex_ >= 0) {
                const QSignalBlocker block(subjectCombo_);
                subjectCombo_->setCurrentIndex(builtSubjectIndex_);
            }
            return;
        }
    }
    // Yeniden uretim BASARISIZ olursa onceki cizim + duzenlemeler korunur (sessiz kayip yok).
    const DrawingDocument prevDoc = doc_;
    const std::vector<DrawingSheet> prevBaseline = baseline_;
    const int prevSheet = currentSheet_;
    const bool prevHave = haveDoc_;
    haveSheet_ = false;
    haveDoc_ = false;
    currentSheet_ = 0;
    edit_.reset();
    view_->setEditor(nullptr);
    view_->setSheet(nullptr);
    if (sheetCombo_) {
        const QSignalBlocker block(sheetCombo_);
        sheetCombo_->clear();
    }
    status_->setStyleSheet(QStringLiteral("color:#a73c3c"));
    const int idx = subjectCombo_->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(subjects_.size())) {
        status_->setText(QStringLiteral("Konu yok"));
        return;
    }
    DrawingOptions o;
    static const SheetSize sizes[] = {SheetSize::A4, SheetSize::A3, SheetSize::A2, SheetSize::A1, SheetSize::A0};
    o.sheet = sizes[std::clamp(sizeCombo_->currentIndex(), 0, 4)];
    o.projection = projectionCombo_->currentIndex() == 1 ? ProjectionMethod::FirstAngle : ProjectionMethod::ThirdAngle;
    o.views.clear();
    if (frontCheck_->isChecked()) o.views.push_back(ViewKind::Front);
    if (topCheck_->isChecked()) o.views.push_back(ViewKind::Top);
    if (sideCheck_->isChecked()) o.views.push_back(ViewKind::Side);
    o.hiddenLines = hiddenCheck_->isChecked();
    o.date = QDate::currentDate().toString(Qt::ISODate).toStdString();
    if (o.views.empty()) {
        status_->setText(QStringLiteral("En az bir g\u00f6r\u00fcn\u00fc\u015f se\u00e7in"));
        return;
    }
    if (setBuilder_) {
        DrawingPreferences p = prefs_;
        p.autoSheetSize = autoSizeCheck_ ? autoSizeCheck_->isChecked() : true;
        if (!p.autoSheetSize) p.sheetSize = o.sheet;
        p.autoMultiSheetFallback = multiSheetCheck_ ? multiSheetCheck_->isChecked() : true;
        p.autoFirst = SheetSize::A3;
        p.autoMax = SheetSize::A0;
        prefs_ = p;
        DrawingSetResult r = setBuilder_(subjects_[static_cast<std::size_t>(idx)].id, o, p);
        if (!r.ok) {
            status_->setStyleSheet(QStringLiteral("color:#a73c3c"));
            QString msg = QStringLiteral("\u00c7izim \u00fcretilemedi: ") + qs(r.error);
            if (!r.issues.empty()) msg += QStringLiteral(" (%1 sorun)").arg(r.issues.size());
            if (prevHave && !prevDoc.sheets().empty()) {   // onceki cizim/duzenlemeler korunur
                doc_ = prevDoc;
                baseline_ = prevBaseline;
                haveDoc_ = true;
                if (subjectCombo_ && builtSubjectIndex_ >= 0) {
                    const QSignalBlocker block(subjectCombo_);
                    subjectCombo_->setCurrentIndex(builtSubjectIndex_);
                }
                if (sheetCombo_) {
                    const QSignalBlocker block(sheetCombo_);
                    sheetCombo_->clear();
                    for (std::size_t i = 0; i < doc_.sheets().size(); ++i)
                        sheetCombo_->addItem(QStringLiteral("%1/%2  %3  %4")
                                                 .arg(i + 1)
                                                 .arg(doc_.sheets().size())
                                                 .arg(qs(doc_.sheets()[i].sheetNumber))
                                                 .arg(QString::fromLatin1(
                                                     sheetSizeName(doc_.sheets()[i].format.size))));
                    sheetCombo_->setCurrentIndex(std::clamp(prevSheet, 0, static_cast<int>(doc_.sheets().size()) - 1));
                }
                showSheet(std::clamp(prevSheet, 0, static_cast<int>(doc_.sheets().size()) - 1));
                msg += QStringLiteral(" \u2014 \u00f6nceki \u00e7izim ve d\u00fczenlemeler korundu");
            }
            status_->setText(msg);
            return;
        }
        int preserved = 0;
        // Karsilastirma tabani: yeniden uretilen TEMIZ paftalar (duzenleme sonradan uygulanir).
        baseline_.clear();
        for (const DrawingSheet& s : r.document.sheets()) baseline_.push_back(s);
        if (!edits.empty()) preserved = mergePreservedEdits(edits, r);
        doc_ = r.document;
        haveDoc_ = true;
        builtSubjectIndex_ = idx;
        {
            const QSignalBlocker block(sheetCombo_);
            for (std::size_t i = 0; i < doc_.sheets().size(); ++i) {
                const DrawingSheet& s = doc_.sheets()[i];
                sheetCombo_->addItem(QStringLiteral("%1/%2  %3  %4")
                                         .arg(i + 1)
                                         .arg(doc_.sheets().size())
                                         .arg(qs(s.sheetNumber))
                                         .arg(QString::fromLatin1(sheetSizeName(s.format.size))));
            }
            sheetCombo_->setCurrentIndex(0);
        }
        showSheet(0);
        status_->setStyleSheet(r.ready ? QStringLiteral("color:#3f7a4f")
                                       : (r.layoutValid ? QStringLiteral("color:#8a6b2a")
                                                        : QStringLiteral("color:#a73c3c")));
        QString text = qs(statusText(r));
        if (preserved > 0) text += QStringLiteral(" — pafta duzenlemeleri korundu (%1)").arg(preserved);
        status_->setText(text);
        return;
    }
    const BuildResult r = builder_(subjects_[static_cast<std::size_t>(idx)].id, o);
    if (!r.ok) {
        status_->setText(QStringLiteral("\u00c7izim \u00fcretilemedi: ") + qs(r.error));
        return;
    }
    sheet_ = r.sheet;
    haveSheet_ = true;
    view_->setSheet(&sheet_);
    view_->fitToWindow();
    status_->setStyleSheet(QStringLiteral("color:#4d687f"));
    status_->setText(QStringLiteral("%1 \u2014 %2 \u2014 \u00f6l\u00e7ek %3 \u2014 %4 g\u00f6r\u00fcn\u00fc\u015f, %5 etiket, %6 \u00f6l\u00e7\u00fc")
                         .arg(qs(sheet_.name), QString::fromLatin1(sheetSizeName(sheet_.format.size)),
                              qs(sheet_.titleBlock.scaleText))
                         .arg(sheet_.views.size())
                         .arg(sheet_.marks.size())
                         .arg(sheet_.dimensions.size()));
}

const DrawingSheet* DrawingPreviewDialog::currentSheet() const noexcept {
    if (edit_) return &edit_->sheet();
    if (haveDoc_ && currentSheet_ >= 0 && currentSheet_ < static_cast<int>(doc_.sheets().size()))
        return &doc_.sheets()[static_cast<std::size_t>(currentSheet_)];
    return haveSheet_ ? &sheet_ : nullptr;
}

void DrawingPreviewDialog::showSheet(int index) {
    if (!haveDoc_ || index < 0 || index >= static_cast<int>(doc_.sheets().size())) return;
    syncEditedSheet(currentSheet_);
    currentSheet_ = index;
    const DrawingSheet& s = doc_.sheets()[static_cast<std::size_t>(index)];
    edit_ = std::make_unique<PaperEditHistory>(s);
    view_->setEditor(edit_.get());
    view_->setFocusPolicy(Qt::StrongFocus);
    if (toolCombo_) {
        const int t = toolCombo_->currentIndex();
        view_->setTool(t == 1 ? DrawingSheetWidget::EditTool::Line
                              : (t == 2 ? DrawingSheetWidget::EditTool::Circle
                                        : (t == 3 ? DrawingSheetWidget::EditTool::Centerline
                                                  : DrawingSheetWidget::EditTool::Select)));
    }
    view_->fitToWindow();
    if (sheetCombo_ && sheetCombo_->currentIndex() != index) {
        const QSignalBlocker block(sheetCombo_);
        sheetCombo_->setCurrentIndex(index);
    }
    status_->setText(QStringLiteral("Pafta %1/%2  %3  %4  \u2014 %5 g\u00f6r\u00fcn\u00fc\u015f, %6 \u00f6l\u00e7\u00fc, %7")
                         .arg(index + 1)
                         .arg(doc_.sheets().size())
                         .arg(qs(s.sheetNumber))
                         .arg(QString::fromLatin1(sheetSizeName(s.format.size)))
                         .arg(s.views.size())
                         .arg(s.dimensions.size())
                         .arg(s.layout.ok ? QStringLiteral("yerle\u015fti") : QStringLiteral("NO_FIT")));
}

void DrawingPreviewDialog::syncEditedSheet(int index) {
    if (!edit_ || !haveDoc_) return;
    if (index >= 0 && index < static_cast<int>(doc_.sheets().size()))
        doc_.sheets()[static_cast<std::size_t>(index)] = edit_->sheet();
}

void DrawingPreviewDialog::onExportPdf() {
    syncEditedSheet(currentSheet_);
    if (haveDoc_) {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("PDF'e aktar (t\u00fcm paftalar)"),
                                                          qs(doc_.sheets().front().sheetNumber) + QStringLiteral(".pdf"),
                                                          QStringLiteral("PDF (*.pdf)"));
        if (path.isEmpty()) return;
        QString err;
        if (!exportDrawingSetPdf(doc_, path, &err)) {
            QMessageBox::warning(this, QStringLiteral("PDF"), err);
            return;
        }
        status_->setText(QStringLiteral("PDF yaz\u0131ld\u0131 (%1 pafta): ").arg(doc_.sheets().size()) + path);
        return;
    }
    if (!haveSheet_) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("PDF'e aktar"),
                                                      qs(sheet_.name) + QStringLiteral(".pdf"),
                                                      QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    QString err;
    if (!exportSheetPdf(sheet_, path, &err)) QMessageBox::warning(this, QStringLiteral("PDF"), err);
    else status_->setText(QStringLiteral("PDF yaz\u0131ld\u0131: ") + path);
}

void DrawingPreviewDialog::onSaveJson() {
    syncEditedSheet(currentSheet_);
    if (!haveDoc_ && !haveSheet_) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("\u00c7izimi JSON olarak kaydet"),
                                                      qs(haveDoc_ ? doc_.sheets().front().sheetNumber : sheet_.name) +
                                                          QStringLiteral(".drawing.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    const std::string payload = [this]() {
        if (haveDoc_) return doc_.serialize();
        DrawingDocument doc;
        doc.addSheet(sheet_);
        return doc.serialize();
    }();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload.c_str()) < 0 || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("JSON"), QStringLiteral("Yaz\u0131lamad\u0131: ") + path);
        return;
    }
    status_->setText(QStringLiteral("JSON yaz\u0131ld\u0131: ") + path);
}

std::vector<SubjectChoice> subjectChoicesFor(const Document& doc, const std::vector<std::size_t>& selected,
                                             bool preferAssembly, int& defaultIndex) {
    defaultIndex = 0;
    std::vector<SubjectChoice> out;
    for (const std::size_t idx : selected) {
        if (idx >= doc.models().size() || !isDrawablePart(doc.models()[idx])) continue;
        const WireframeModel& m = doc.models()[idx];
        const EntityId id = doc.modelId(idx);
        QString name = QString::fromStdString(m.properties().name);
        if (name.isEmpty()) name = QString::fromStdString(m.properties().profileName);
        if (name.isEmpty()) name = QStringLiteral("Eleman");
        out.push_back({QStringLiteral("Par\u00e7a: %1 (#%2)").arg(name).arg(id), id});
        if (const auto asmId = doc.assemblyIdContaining(id)) {
            const auto ai = doc.indexOfAssembly(*asmId);
            const QString an = ai ? QString::fromStdWString(doc.assemblies()[*ai].name) : QStringLiteral("?");
            out.push_back({QStringLiteral("Assembly: %1").arg(an), *asmId});
            if (preferAssembly || selected.size() > 1) defaultIndex = 1;
        }
        break; // yalniz ilk cizilebilir secili parca konu olur
    }
    return out;
}

bool showDrawingDialog(QWidget* parent, const Document& doc, const std::vector<std::size_t>& selected,
                       bool preferAssembly) {
    int def = 0;
    std::vector<SubjectChoice> subjects = subjectChoicesFor(doc, selected, preferAssembly, def);
    if (subjects.empty()) {
        QMessageBox::information(parent, QStringLiteral("\u00c7izim"),
                                 QStringLiteral("\u00d6nce \u00e7izilecek bir par\u00e7a ya da assembly se\u00e7in."));
        return false;
    }
    auto registry = std::make_shared<MarkRegistry>();
    DrawingPreviewDialog dlg(
        std::move(subjects), def,
        [&doc, registry](EntityId subject, const DrawingOptions& o) { return buildSheet(doc, *registry, subject, o); },
        parent,
        [&doc, registry](EntityId subject, const DrawingOptions& o, const DrawingPreferences& p) {
            return buildDrawingSet(doc, *registry, subject, o, p);
        });
    dlg.exec();
    return true;
}

} // namespace mm::drawing
