#include "model_maker/drawing_sheet_widget.hpp"

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

// Metin: konum (sayfa mm, y yukari), yukseklik mm, hizalama, aci (derece, saat yonu tersi).
void drawText(QPainter& p, const QPointF& at, const QString& t, double heightMm, Qt::Alignment al,
              double angleDeg = 0.0, bool bold = false) {
    p.save();
    p.translate(at);
    if (angleDeg != 0.0) p.rotate(angleDeg);
    p.scale(heightMm / 40.0, -heightMm / 40.0);
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

double textWidthMm(const QString& t, double heightMm, bool bold = false) {
    QFont f;
    f.setPixelSize(40);
    f.setBold(bold);
    return QFontMetricsF(f).horizontalAdvance(t) * heightMm / 40.0;
}

QString valueText(double v) {
    const double r = std::round(v);
    return std::fabs(v - r) < 0.05 ? QString::number(static_cast<long long>(r))
                                   : QString::number(v, 'f', 1);
}

void tick(QPainter& p, const QPointF& at) {
    p.drawLine(QPointF(at.x() - 1.2, at.y() - 1.2), QPointF(at.x() + 1.2, at.y() + 1.2));
}

void drawTitleBlock(QPainter& p, const DrawingSheet& s) {
    const SheetFormat& f = s.format;
    const QRectF box(f.width - f.margin - f.titleWidth, f.margin, f.titleWidth, f.titleHeight);
    p.setPen(QPen(Qt::black, 0.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
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
    const DrawingView* v = s.findView(d.viewId);
    if (!v) return;
    const QPointF a = pt(v->toPaper(d.p1)), b = pt(v->toPaper(d.p2));
    const Bounds2 pb = v->paperBounds();
    p.setPen(QPen(QColor(0, 90, 160), 0.18));
    const QString label = valueText(d.value);
    constexpr double kText = 3.2;
    if (d.direction == DimensionDirection::Horizontal) {
        const double y = pb.min.y - d.offset;
        p.drawLine(QPointF(a.x(), a.y()), QPointF(a.x(), y - 1.5));
        p.drawLine(QPointF(b.x(), b.y()), QPointF(b.x(), y - 1.5));
        p.drawLine(QPointF(a.x(), y), QPointF(b.x(), y));
        tick(p, QPointF(a.x(), y));
        tick(p, QPointF(b.x(), y));
        drawText(p, QPointF((a.x() + b.x()) / 2, y + 0.8), label, kText, Qt::AlignHCenter | Qt::AlignBottom);
    } else if (d.direction == DimensionDirection::Vertical) {
        const double x = pb.max.x + d.offset;
        p.drawLine(QPointF(a.x(), a.y()), QPointF(x + 1.5, a.y()));
        p.drawLine(QPointF(b.x(), b.y()), QPointF(x + 1.5, b.y()));
        p.drawLine(QPointF(x, a.y()), QPointF(x, b.y()));
        tick(p, QPointF(x, a.y()));
        tick(p, QPointF(x, b.y()));
        drawText(p, QPointF(x - 2.2, (a.y() + b.y()) / 2), label, kText, Qt::AlignCenter, 90.0);
    } else {
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::hypot(dx, dy);
        if (len < 1e-9) return;
        double nx = -dy / len, ny = dx / len;
        const Vec2 c = pb.centre();
        if (nx * ((a.x() + b.x()) / 2 - c.x) + ny * ((a.y() + b.y()) / 2 - c.y) < 0) { nx = -nx; ny = -ny; }
        const QPointF a2(a.x() + nx * d.offset, a.y() + ny * d.offset);
        const QPointF b2(b.x() + nx * d.offset, b.y() + ny * d.offset);
        p.drawLine(a, QPointF(a2.x() + nx * 1.5, a2.y() + ny * 1.5));
        p.drawLine(b, QPointF(b2.x() + nx * 1.5, b2.y() + ny * 1.5));
        p.drawLine(a2, b2);
        tick(p, a2);
        tick(p, b2);
        double ang = std::atan2(dy, dx) * 180.0 / kPi;
        if (ang > 90.0) ang -= 180.0;
        if (ang <= -90.0) ang += 180.0;
        drawText(p, QPointF((a2.x() + b2.x()) / 2 + nx * 2.2, (a2.y() + b2.y()) / 2 + ny * 2.2), label, kText,
                 Qt::AlignCenter, ang);
    }
}

void drawMark(QPainter& p, const DrawingSheet& s, const MarkAnnotation& m) {
    const DrawingView* v = s.findView(m.viewId);
    if (!v) return;
    const QString label = qs(m.text);
    const double h = m.isAssembly ? 5.0 : 3.5;
    const double w = textWidthMm(label, h, m.isAssembly) + 3.0;
    const QRectF box(m.labelPos.x, m.labelPos.y, std::max(w, h + 3.0), h + 2.0);
    const QPointF anchor = pt(v->toPaper(m.anchor));
    const QColor col = m.isAssembly ? QColor(170, 40, 40) : QColor(30, 110, 50);
    p.setPen(QPen(col, 0.18));
    p.drawLine(anchor, box.center());
    p.setBrush(col);
    p.drawEllipse(anchor, 0.7, 0.7);
    p.setBrush(Qt::white);
    p.setPen(QPen(col, m.isAssembly ? 0.45 : 0.3));
    p.drawRoundedRect(box, 1.0, 1.0);
    drawText(p, box.center(), label, h, Qt::AlignCenter, 0.0, m.isAssembly);
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
    painter.setPen(QPen(Qt::black, 0.7));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(f.margin, f.margin, f.width - 2 * f.margin, f.height - 2 * f.margin));
    drawTitleBlock(painter, sheet);

    for (const DrawingView& v : sheet.views) {
        for (const Segment2& seg : v.segments) {
            if (seg.style == LineStyle::Visible) {
                painter.setPen(QPen(Qt::black, 0.35, Qt::SolidLine, Qt::RoundCap));
            } else {
                QPen pen(QColor(110, 110, 110), 0.18, Qt::DashLine);
                pen.setDashPattern({6.0, 4.0});
                painter.setPen(pen);
            }
            painter.drawLine(pt(v.toPaper(seg.a)), pt(v.toPaper(seg.b)));
        }
        const Bounds2 pb = v.paperBounds();
        painter.setPen(QPen(Qt::black, 0.1));
        drawText(painter, QPointF(pb.max.x, pb.max.y + 3.0),
                 qs(v.label) + QStringLiteral("  ") + qs(sheet.titleBlock.scaleText), 3.0,
                 Qt::AlignRight | Qt::AlignBottom);
    }
    for (const Dimension2D& d : sheet.dimensions) drawDimension(painter, sheet, d);
    for (const MarkAnnotation& m : sheet.marks) drawMark(painter, sheet, m);
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

void DrawingSheetWidget::mousePressEvent(QMouseEvent* e) { lastPos_ = e->pos(); }

void DrawingSheetWidget::mouseDoubleClickEvent(QMouseEvent*) { fitToWindow(); }

void DrawingSheetWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) {
        pan_ += QPointF(e->pos() - lastPos_);
        lastPos_ = e->pos();
        fitted_ = false;
        update();
    }
}

void DrawingSheetWidget::resizeEvent(QResizeEvent*) { update(); }

// ---------------------------------------------------------------------------------------

DrawingPreviewDialog::DrawingPreviewDialog(std::vector<SubjectChoice> subjects, int defaultSubject,
                                           Builder builder, QWidget* parent)
    : QDialog(parent), subjects_(std::move(subjects)), builder_(std::move(builder)) {
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
    rebuild();
}

void DrawingPreviewDialog::rebuild() {
    haveSheet_ = false;
    view_->setSheet(nullptr);
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

void DrawingPreviewDialog::onExportPdf() {
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
    if (!haveSheet_) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("\u00c7izimi JSON olarak kaydet"),
                                                      qs(sheet_.name) + QStringLiteral(".drawing.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    DrawingDocument doc;
    doc.addSheet(sheet_);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(doc.serialize().c_str()) < 0 || !file.commit()) {
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
        parent);
    dlg.exec();
    return true;
}

} // namespace mm::drawing
