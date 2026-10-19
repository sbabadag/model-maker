#include "model_maker/component_dialog_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm {

namespace {

// specs içinde ad -> spec (component_engine.hpp findParamSpec'e devreder).
const CompParamSpec* specByName(const std::vector<CompParamSpec>& specs,
                                const std::wstring& name) noexcept {
    return findParamSpec(specs, name);
}

} // namespace

// --- ComponentSchematicView ---------------------------------------------------

ComponentSchematicView::ComponentSchematicView(const ComponentDialogSchema& schema,
                                               QWidget* parent)
    : QWidget(parent), schema_(schema) {
    setMinimumSize(260, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Dünya sınırlarını hesapla: tüm ilkel köşeleri + hotspot konumları.
    minX_ = minY_ = std::numeric_limits<double>::max();
    maxX_ = maxY_ = std::numeric_limits<double>::lowest();
    const auto include = [&](double x, double y) {
        minX_ = std::min(minX_, x);
        minY_ = std::min(minY_, y);
        maxX_ = std::max(maxX_, x);
        maxY_ = std::max(maxY_, y);
    };
    for (const auto& item : schema_.items) {
        for (const auto& pr : item.primitives) {
            switch (pr.kind) {
                case DiagramPrimitiveKind::Rect:
                case DiagramPrimitiveKind::Line:
                    include(pr.a.x, pr.a.y);
                    include(pr.b.x, pr.b.y);
                    break;
                case DiagramPrimitiveKind::Circle:
                    include(pr.a.x - pr.r, pr.a.y - pr.r);
                    include(pr.a.x + pr.r, pr.a.y + pr.r);
                    break;
                case DiagramPrimitiveKind::Text:
                    include(pr.a.x, pr.a.y);
                    break;
                case DiagramPrimitiveKind::Polyline:
                    for (const auto& pt : pr.pts) include(pt.x, pt.y);
                    break;
            }
        }
    }
    for (const auto& h : schema_.hotspots) include(h.position.x, h.position.y);
    if (maxX_ < minX_) {  // hiç içerik yok
        minX_ = minY_ = 0.0;
        maxX_ = maxY_ = 1.0;
    }
}

void ComponentSchematicView::setHighlightedParam(const std::wstring& paramName) {
    if (highlightedParam_ == paramName) return;
    highlightedParam_ = paramName;
    update();
}

void ComponentSchematicView::recomputeTransform() {
    const double w = width();
    const double h = height();
    if (w <= 1.0 || h <= 1.0) return;
    const double pad = 22.0;
    const double ww = std::max(1e-9, maxX_ - minX_);
    const double wh = std::max(1e-9, maxY_ - minY_);
    scale_ = std::min((w - 2.0 * pad) / ww, (h - 2.0 * pad) / wh);
    if (!(scale_ > 0.0)) scale_ = 1.0;
    const double sw = ww * scale_;
    const double sh = wh * scale_;
    offsetX_ = (w - sw) / 2.0;
    offsetY_ = (h - sh) / 2.0;
}

QPointF ComponentSchematicView::toScreen(const DiagramPoint& w) const {
    return QPointF(offsetX_ + (w.x - minX_) * scale_,
                   offsetY_ + (maxY_ - w.y) * scale_);  // şema y yukarı, Qt y aşağı
}

DiagramPoint ComponentSchematicView::toWorld(const QPointF& s) const {
    return DiagramPoint{minX_ + (s.x() - offsetX_) / scale_,
                        maxY_ - (s.y() - offsetY_) / scale_};
}

void ComponentSchematicView::paintEvent(QPaintEvent*) {
    recomputeTransform();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0xFA, 0xFA, 0xFC));

    // Çizim ögeleri (items).
    for (const auto& item : schema_.items) {
        for (const auto& pr : item.primitives) {
            QPen pen(QColor(0x40, 0x40, 0x50));
            pen.setWidthF(1.4);
            if (pr.dashed) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            const QBrush fillBrush = pr.filled ? QBrush(QColor(0xD0, 0xD8, 0xE8))
                                               : QBrush(Qt::NoBrush);
            p.setBrush(fillBrush);

            switch (pr.kind) {
                case DiagramPrimitiveKind::Rect: {
                    const QPointF a = toScreen(pr.a);
                    const QPointF b = toScreen(pr.b);
                    const QRectF r(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                                   QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
                    p.drawRect(r);
                    break;
                }
                case DiagramPrimitiveKind::Line:
                    p.drawLine(toScreen(pr.a), toScreen(pr.b));
                    break;
                case DiagramPrimitiveKind::Circle: {
                    const QPointF c = toScreen(pr.a);
                    const double rr = pr.r * scale_;
                    p.drawEllipse(c, rr, rr);
                    break;
                }
                case DiagramPrimitiveKind::Text: {
                    QFont f = p.font();
                    f.setPointSizeF(7.5);
                    p.setFont(f);
                    p.setPen(QColor(0x30, 0x30, 0x40));
                    p.drawText(toScreen(pr.a), QString::fromStdWString(pr.text));
                    break;
                }
                case DiagramPrimitiveKind::Polyline: {
                    if (pr.pts.size() < 2) break;
                    QPolygonF poly;
                    poly.reserve(static_cast<int>(pr.pts.size()));
                    for (const auto& pt : pr.pts) poly << toScreen(pt);
                    if (pr.closed) p.drawPolygon(poly);
                    else p.drawPolyline(poly);
                    break;
                }
            }
        }
    }

    // Hotspot'lar.
    for (const auto& h : schema_.hotspots) {
        const QPointF c = toScreen(h.position);
        const bool hl = h.paramName.has_value() && *h.paramName == highlightedParam_;
        const double rr = std::max(4.0, h.radius * scale_);
        p.setBrush(hl ? QColor(0xE8, 0x8A, 0x3A) : QColor(0xFF, 0xFF, 0xFF));
        p.setPen(hl ? QPen(QColor(0xC0, 0x50, 0x00), 2.0) : QPen(QColor(0x30, 0x30, 0x40), 1.2));
        p.drawEllipse(c, rr, rr);
        if (h.label.has_value()) {
            QFont f = p.font();
            f.setBold(true);
            f.setPointSizeF(7.0);
            p.setFont(f);
            p.setPen(QColor(0x30, 0x30, 0x40));
            p.drawText(QPointF(c.x() + rr + 2.0, c.y() + 3.0),
                       QString::fromStdWString(*h.label));
        }
    }
}

void ComponentSchematicView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const DiagramPoint w = toWorld(e->position());
        const auto id = hitTestDiagramHotspot(schema_, w);
        if (id) {
            const DiagramHotspot* h = findHotspot(schema_, *id);
            QString param;
            if (h && h->paramName.has_value())
                param = QString::fromStdWString(*h->paramName);
            emit hotspotClicked(param);
        }
    }
    QWidget::mousePressEvent(e);
}

// --- ComponentDialogWidget ----------------------------------------------------

ComponentDialogWidget::ComponentDialogWidget(const ComponentDialogDefinition& def,
                                             const std::vector<CompParam>& current,
                                             QWidget* parent)
    : QWidget(parent), def_(def), current_(current) {
    // param -> hotspot id'leri (ters bağ).
    for (const auto& h : def_.schema.hotspots) {
        if (h.paramName.has_value())
            paramToHotspots_[*h.paramName].push_back(h.id);
    }

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    schematic_ = new ComponentSchematicView(def_.schema, this);
    schematic_->setMinimumWidth(300);
    root->addWidget(schematic_, /*stretch*/ 3);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setMinimumWidth(380);
    paramBody_ = new QWidget;
    paramLayout_ = new QVBoxLayout(paramBody_);
    paramLayout_->setContentsMargins(4, 4, 4, 4);
    paramLayout_->setSpacing(6);

    // Grup başına bir bölüm; grupta olmayan spec'ler "Diğer" bölümüne.
    std::vector<std::wstring> ungrouped;
    for (const auto& spec : def_.specs) ungrouped.push_back(spec.name);

    const auto addRow = [&](QFormLayout* form, QWidget* box, const std::wstring& pn) {
        const CompParamSpec* spec = specByName(def_.specs, pn);
        if (!spec) return;
        ungrouped.erase(std::remove(ungrouped.begin(), ungrouped.end(), pn), ungrouped.end());
        const CompParam* stored = findParam(current_, pn);
        const CompParamValue value = stored ? stored->value : paramDefaultValue(*spec);
        QWidget* editor = buildEditor(*spec, value, box);
        auto* lbl = new QLabel(QString::fromStdWString(
            spec->description.empty() ? spec->name : spec->description), box);
        form->addRow(lbl, editor);
        editors_[pn] = editor;
        rows_[pn] = lbl;
        editor->installEventFilter(this);
    };

    for (const auto& g : def_.schema.groups) {
        auto* box = new QGroupBox(QString::fromStdWString(g.title), paramBody_);
        auto* form = new QFormLayout(box);
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const auto& pn : g.paramNames) addRow(form, box, pn);
        paramLayout_->addWidget(box);
    }

    if (!ungrouped.empty()) {
        auto* box = new QGroupBox(QStringLiteral("Diğer"), paramBody_);
        auto* form = new QFormLayout(box);
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const auto& pn : ungrouped) addRow(form, box, pn);
        paramLayout_->addWidget(box);
    }

    paramLayout_->addStretch(1);
    scroll_->setWidget(paramBody_);
    root->addWidget(scroll_, /*stretch*/ 2);

    connect(schematic_, &ComponentSchematicView::hotspotClicked,
            this, &ComponentDialogWidget::onHotspotClicked);
}

QWidget* ComponentDialogWidget::buildEditor(const CompParamSpec& spec,
                                            const CompParamValue& value,
                                            QWidget* parent) {
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle: {
            auto* spin = new QDoubleSpinBox(parent);
            spin->setDecimals(2);
            // Açık aralıkta sentinel'leri (örn. -1e9 = "tanımsız") korumak için
            // geniş sınır; -1e6'ya kelepçelemek sentinel'i SESSIZ bozar.
            spin->setRange(spec.hasMinDouble ? spec.minDouble : -1e12,
                           spec.hasMaxDouble ? spec.maxDouble : 1e12);
            double v = spec.defDouble;
            if (const auto* d = std::get_if<double>(&value)) v = *d;
            spin->setValue(v);
            if (!spec.unit.empty())
                spin->setSuffix(QStringLiteral(" ") + QString::fromStdWString(spec.unit));
            return spin;
        }
        case CompParamType::Integer: {
            auto* spin = new QSpinBox(parent);
            spin->setRange(spec.hasMinInt ? spec.minInt : std::numeric_limits<int>::min(),
                           spec.hasMaxInt ? spec.maxInt : std::numeric_limits<int>::max());
            int v = spec.defInt;
            if (const auto* n = std::get_if<int>(&value)) v = *n;
            spin->setValue(v);
            return spin;
        }
        case CompParamType::Boolean: {
            auto* box = new QCheckBox(parent);
            bool v = spec.defBool;
            if (const auto* b = std::get_if<bool>(&value)) v = *b;
            box->setChecked(v);
            return box;
        }
        case CompParamType::Enum: {
            auto* combo = new QComboBox(parent);
            for (const auto& choice : spec.choices)
                combo->addItem(QString::fromStdWString(choice));
            std::wstring v = spec.defString;
            if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
            const int idx = combo->findText(QString::fromStdWString(v));
            if (idx >= 0) combo->setCurrentIndex(idx);
            return combo;
        }
        case CompParamType::String:
        case CompParamType::Profile:
        case CompParamType::Material: {
            auto* edit = new QLineEdit(parent);
            std::wstring v = spec.defString;
            if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
            edit->setText(QString::fromStdWString(v));
            return edit;
        }
        case CompParamType::EntityRef: {
            // Varlık referansı bu diyalogdan düzenlenmez (model seçimi ister).
            auto* edit = new QLineEdit(parent);
            edit->setReadOnly(true);
            unsigned long long v = 0;
            if (const auto* e = std::get_if<EntityId>(&value)) v = static_cast<unsigned long long>(*e);
            edit->setText(QString::number(v));
            return edit;
        }
    }
    return nullptr;
}

CompParamValue ComponentDialogWidget::valueFromEditor(QWidget* editor,
                                                      const CompParamSpec& spec) {
    if (!editor) return paramDefaultValue(spec);
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle:
            return static_cast<QDoubleSpinBox*>(editor)->value();
        case CompParamType::Integer:
            return static_cast<QSpinBox*>(editor)->value();
        case CompParamType::Boolean:
            return static_cast<QCheckBox*>(editor)->isChecked();
        case CompParamType::Enum:
            return static_cast<QComboBox*>(editor)->currentText().toStdWString();
        case CompParamType::EntityRef:
            return paramDefaultValue(spec);  // salt-okunur
        case CompParamType::String:
        case CompParamType::Profile:
        case CompParamType::Material:
        default:
            return static_cast<QLineEdit*>(editor)->text().toStdWString();
    }
}

std::vector<CompParam> ComponentDialogWidget::resultParams() const {
    std::vector<CompParam> result = current_;
    for (const auto& spec : def_.specs) {
        const auto it = editors_.find(spec.name);
        if (it == editors_.end()) continue;
        const CompParamValue v = valueFromEditor(it->second, spec);
        bool found = false;
        for (auto& p : result) {
            if (p.name == spec.name) {
                p.value = v;
                found = true;
                break;
            }
        }
        if (!found) result.push_back(CompParam{spec.name, v});
    }
    return result;
}

void ComponentDialogWidget::onHotspotClicked(const QString& paramName) {
    highlightParamRow(paramName.toStdWString());
}

bool ComponentDialogWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        for (const auto& [name, editor] : editors_) {
            if (editor == watched) {
                highlightParamRow(name);
                schematic_->setHighlightedParam(name);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ComponentDialogWidget::highlightParamRow(const std::wstring& paramName) {
    if (!highlightedRow_.empty()) {
        const auto it = rows_.find(highlightedRow_);
        if (it != rows_.end()) it->second->setStyleSheet(QString());
    }
    highlightedRow_ = paramName;
    if (paramName.empty()) return;
    const auto it = rows_.find(paramName);
    if (it != rows_.end()) {
        it->second->setStyleSheet(QStringLiteral(
            "background: #FFE3B3; border-radius: 2px; padding: 1px;"));
        const auto eit = editors_.find(paramName);
        if (eit != editors_.end()) scroll_->ensureWidgetVisible(eit->second, 0, 16);
    }
}

} // namespace mm
