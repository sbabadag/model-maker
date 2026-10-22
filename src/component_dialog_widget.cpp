#include "model_maker/component_dialog_widget.hpp"

#include <QAbstractItemView>
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
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
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

// Sekme formunun altina aciklayici sekilleri (Tekla yardim sayfasi olcu
// sekilleri) ekler. Sekil yuklenemezse (kaynak gomulu degil) o sekil atlanir;
// hic sekil yoksa form oldugu gibi doner. Sekiller parametre DEGILDIR.
QWidget* wrapWithFigures(QWidget* form, const ComponentDialogGroup& g) {
    if (g.figures.empty()) return form;
    auto* box = new QGroupBox(QStringLiteral("Tekla 146 sekilleri"));
    auto* v = new QVBoxLayout(box);
    int shown = 0;
    for (const auto& f : g.figures) {
        QPixmap pm(QString::fromStdWString(f.resource));
        if (pm.isNull()) continue;
        // Kucuk simge boyutundaki sekilleri okunur yap (en fazla 2x), buyukleri
        // sinirla; oran korunur.
        const int w = pm.width();
        const int target = w < 150 ? w * 2 : std::min(w, 420);
        if (target != w) pm = pm.scaledToWidth(target, Qt::SmoothTransformation);
        auto* cap = new QLabel(QString::fromStdWString(f.caption), box);
        cap->setStyleSheet(QStringLiteral("font-weight: bold;"));
        auto* img = new QLabel(box);
        img->setPixmap(pm);
        img->setToolTip(QString::fromStdWString(f.caption));
        v->addWidget(cap);
        v->addWidget(img);
        ++shown;
    }
    if (shown == 0) {
        delete box;
        return form;
    }
    auto* outer = new QWidget;
    auto* ol = new QVBoxLayout(outer);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(form);
    ol->addWidget(box);
    ol->addStretch(1);
    return outer;
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
        const bool hlParam = h.paramName.has_value() && *h.paramName == highlightedParam_;
        const bool hlWeld = h.weldRow.has_value() && *h.weldRow == highlightedWeldRow_;
        const bool hl = hlParam || hlWeld;
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
            // Kaynak cagrisi ise tablo satirini sec; degilse parametre vurgusu.
            if (h && h->weldRow.has_value()) {
                setHighlightedWeldRow(*h->weldRow);
                emit weldCalloutClicked(*h->weldRow);
            } else {
                QString param;
                if (h && h->paramName.has_value())
                    param = QString::fromStdWString(*h->paramName);
                emit hotspotClicked(param);
            }
        }
    }
    QWidget::mousePressEvent(e);
}

void ComponentSchematicView::setHighlightedWeldRow(int row) {
    if (highlightedWeldRow_ == row) return;
    highlightedWeldRow_ = row;
    update();
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

    if (def_.schema.useTabs) {
        // SEKMELI duzen (Tekla 146): her grup bir SEKME. Ayni parametre birden
        // fazla sekmede listelenebilir (or. plateThickness hem Picture hem
        // Plates) ama TEK editoru olur — ilk sekmede acilir, digeri atlanir;
        // aksi halde iki editor ayni degeri paylasmaz ve son yazan kazanir.
        tabs_ = new QTabWidget(this);
        tabs_->setMinimumWidth(380);
        for (const auto& g : def_.schema.groups) {
            auto* page = new QWidget;
            auto* form = new QFormLayout(page);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            for (const auto& pn : g.paramNames) addParamRow(form, page, pn, ungrouped);
            if (form->rowCount() == 0) {  // bos sekme acma
                delete page;
                continue;
            }
            // Sekme icerigi KAYDIRILABILIR: 146 "Bolts" sekmesi 20+ satir tasir.
            auto* pageScroll = new QScrollArea;
            pageScroll->setWidgetResizable(true);
            pageScroll->setWidget(wrapWithFigures(page, g));
            tabs_->addTab(pageScroll, QString::fromStdWString(g.title));
            const int idx = tabs_->count() - 1;
            for (const auto& pn : g.paramNames)
                if (editors_.count(pn) && !paramTabIndex_.count(pn)) paramTabIndex_[pn] = idx;
        }
        if (!ungrouped.empty()) {
            auto* page = new QWidget;
            auto* form = new QFormLayout(page);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            for (const auto& pn : ungrouped) addParamRow(form, page, pn, ungrouped);
            if (form->rowCount() > 0) {
                auto* pageScroll = new QScrollArea;
                pageScroll->setWidgetResizable(true);
                pageScroll->setWidget(page);
                tabs_->addTab(pageScroll, QStringLiteral("Diğer"));
            } else {
                delete page;
            }
        }
        root->addWidget(tabs_, /*stretch*/ 2);
        // Tekla "Welds": kaynak tablosu ayri sekme; semadaki 1..N cagrilariyla
        // cift yonlu eslesir (satir sec -> cagri vurgulanir, cagri tikla -> satir).
        buildWeldTable(tabs_);
    } else {
        scroll_ = new QScrollArea(this);
        scroll_->setWidgetResizable(true);
        scroll_->setMinimumWidth(380);
        paramBody_ = new QWidget;
        paramLayout_ = new QVBoxLayout(paramBody_);
        paramLayout_->setContentsMargins(4, 4, 4, 4);
        paramLayout_->setSpacing(6);

        for (const auto& g : def_.schema.groups) {
            auto* box = new QGroupBox(QString::fromStdWString(g.title), paramBody_);
            auto* form = new QFormLayout(box);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            for (const auto& pn : g.paramNames) addParamRow(form, box, pn, ungrouped);
            paramLayout_->addWidget(box);
        }

        if (!ungrouped.empty()) {
            auto* box = new QGroupBox(QStringLiteral("Diğer"), paramBody_);
            auto* form = new QFormLayout(box);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            for (const auto& pn : ungrouped) addParamRow(form, box, pn, ungrouped);
            paramLayout_->addWidget(box);
        }

        paramLayout_->addStretch(1);
        scroll_->setWidget(paramBody_);
        root->addWidget(scroll_, /*stretch*/ 2);
    }

    connect(schematic_, &ComponentSchematicView::hotspotClicked,
            this, &ComponentDialogWidget::onHotspotClicked);
}

// Tek parametre satiri: [Default onay kutusu] etiket | editor.
// `optional` parametrelerde onay kutusu isaretsizse deger sonuca YAZILMAZ
// (motor varsayilani uygulanir) — Tekla'daki "Default" davranisi.
void ComponentDialogWidget::addParamRow(QFormLayout* form, QWidget* box,
                                        const std::wstring& pn,
                                        std::vector<std::wstring>& ungrouped) {
    const CompParamSpec* spec = specByName(def_.specs, pn);
    if (!spec) return;
    ungrouped.erase(std::remove(ungrouped.begin(), ungrouped.end(), pn), ungrouped.end());
    if (editors_.count(pn)) return;  // ayni parametre icin TEK editor

    const CompParam* stored = findParam(current_, pn);
    const CompParamValue value = stored ? stored->value : paramDefaultValue(*spec);
    QWidget* editor = buildEditor(*spec, value, box);

    auto* labelHost = new QWidget(box);
    auto* labelRow = new QHBoxLayout(labelHost);
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(4);
    if (spec->optional) {
        // Isaretli = deger kullanilir; bos = Default (motor varsayilani).
        auto* cb = new QCheckBox(labelHost);
        cb->setChecked(true);  // Tekla varsayilani: isaretli
        cb->setToolTip(QStringLiteral("Isaretsiz = Default (motor varsayilani)"));
        optionalChecks_[pn] = cb;
        labelRow->addWidget(cb);
    }
    auto* lbl = new QLabel(QString::fromStdWString(
        spec->description.empty() ? spec->name : spec->description), labelHost);
    labelRow->addWidget(lbl, 1);

    if (spec->documentedOnly) {
        // Tekla'da var, bizde geometrisi yok: goster ama kilitle.
        const QString hint = QStringLiteral(
            "Tekla 146 alani - henuz geometriye etkisi yok (deger kaydedilmez)");
        editor->setEnabled(false);
        editor->setToolTip(hint);
        lbl->setEnabled(false);
        lbl->setToolTip(hint);
    }

    form->addRow(labelHost, editor);
    editors_[pn] = editor;
    rows_[pn] = lbl;
    editor->installEventFilter(this);
}

// Tekla "Welds" tablosu: 12 sutun (No/Size/Type/Angle/Contour/Finish/Eff.thr./
// R.opng./Placement/Prep./Length/Pitch). No sutununda onay kutusu + numara.
// Satir secimi semadaki ayni numarali cagriyi vurgular; cagriya tiklamak satiri secer.
void ComponentDialogWidget::buildWeldTable(QTabWidget* tabs) {
    if (def_.schema.weldRows.empty()) return;
    const int n = static_cast<int>(def_.schema.weldRows.size());
    weldTable_ = new QTableWidget(n, 12, tabs);
    const wchar_t* headers[] = {L"No",       L"Size",   L"Type",   L"Angle",
                                L"Contour",  L"Finish", L"Eff.thr.", L"R.opng.",
                                L"Placement", L"Prep.", L"Length", L"Pitch"};
    QStringList labels;
    for (const wchar_t* h : headers) labels << QString::fromStdWString(h);
    weldTable_->setHorizontalHeaderLabels(labels);
    weldTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    weldTable_->setSelectionMode(QAbstractItemView::SingleSelection);

    for (int r = 0; r < n; ++r) {
        const WeldTableRow& row = def_.schema.weldRows[static_cast<std::size_t>(r)];
        auto* no = new QTableWidgetItem(QString::number(row.number));
        no->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        no->setCheckState(row.enabled ? Qt::Checked : Qt::Unchecked);
        weldTable_->setItem(r, 0, no);
        const auto num = [&](int c, double v) {
            weldTable_->setItem(r, c, new QTableWidgetItem(QString::number(v, 'f', 1)));
        };
        num(1, row.size);
        weldTable_->setItem(r, 2, new QTableWidgetItem(
            row.type == WeldRowType::Fillet ? QStringLiteral("Fillet") : QStringLiteral("Butt")));
        num(3, row.angle);
        weldTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdWString(row.contour)));
        weldTable_->setItem(r, 5, new QTableWidgetItem(QString::fromStdWString(row.finish)));
        num(6, row.effThroat);
        num(7, row.rootOpening);
        weldTable_->setItem(r, 8, new QTableWidgetItem(QString::fromStdWString(row.placement)));
        weldTable_->setItem(r, 9, new QTableWidgetItem(QString::fromStdWString(row.prep)));
        num(10, row.length);
        num(11, row.pitch);
    }
    weldTable_->resizeColumnsToContents();
    weldTabIndex_ = tabs->addTab(weldTable_, QStringLiteral("Welds"));

    QObject::connect(weldTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!schematic_ || !weldTable_) return;
        const auto rows = weldTable_->selectionModel()->selectedRows();
        schematic_->setHighlightedWeldRow(rows.isEmpty() ? -1 : rows.first().row());
    });
    // Semadaki numarali cagri -> tablo satiri (ve Welds sekmesine gecis).
    QObject::connect(schematic_, &ComponentSchematicView::weldCalloutClicked, this,
                     [this](int row) {
                         if (!weldTable_) return;
                         if (tabs_) tabs_->setCurrentIndex(weldTabIndex_);
                         weldTable_->selectRow(row);
                         if (auto* it = weldTable_->item(row, 0))
                             weldTable_->scrollToItem(it);
                     });
}

// Tablodaki duzenlenmis kaynak satirlarini geri okur (kaydetmek isteyen cagiran icin).
std::vector<WeldTableRow> ComponentDialogWidget::resultWeldRows() const {
    std::vector<WeldTableRow> out = def_.schema.weldRows;
    if (!weldTable_) return out;
    for (std::size_t r = 0; r < out.size(); ++r) {
        const int row = static_cast<int>(r);
        auto text = [&](int c) {
            auto* it = weldTable_->item(row, c);
            return it ? it->text() : QString();
        };
        auto value = [&](int c, double fallback) {
            bool ok = false;
            const double v = text(c).toDouble(&ok);
            return ok ? v : fallback;
        };
        auto* no = weldTable_->item(row, 0);
        out[r].enabled = no ? no->checkState() == Qt::Checked : out[r].enabled;
        out[r].number = row + 1;
        out[r].size = value(1, out[r].size);
        out[r].type = text(2).compare(QStringLiteral("Butt"), Qt::CaseInsensitive) == 0
                          ? WeldRowType::Butt
                          : WeldRowType::Fillet;
        out[r].angle = value(3, out[r].angle);
        out[r].contour = text(4).toStdWString();
        out[r].finish = text(5).toStdWString();
        out[r].effThroat = value(6, out[r].effThroat);
        out[r].rootOpening = value(7, out[r].rootOpening);
        out[r].placement = text(8).toStdWString();
        out[r].prep = text(9).toStdWString();
        out[r].length = value(10, out[r].length);
        out[r].pitch = value(11, out[r].pitch);
    }
    return out;
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
        if (spec.documentedOnly) continue;  // motor okumaz; sonuca yazilmaz
        // "Default" onay kutusu isaretsizse deger SONUCA YAZILMAZ: motor kendi
        // varsayilanini uygular (Tekla davranisi). Onceden kayitli deger varsa
        // da kaldirilir — aksi halde "Default" secmek eski degeri korurdu.
        const auto oc = optionalChecks_.find(spec.name);
        if (oc != optionalChecks_.end() && !oc->second->isChecked()) {
            result.erase(std::remove_if(result.begin(), result.end(),
                                        [&](const CompParam& p) { return p.name == spec.name; }),
                         result.end());
            continue;
        }
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
    const std::wstring name = paramName.toStdWString();
    // Sekmeli duzende hotspot'un parametresi baska bir sekmede olabilir:
    // once o sekmeye gec, sonra satiri vurgula.
    if (tabs_) {
        const auto it = paramTabIndex_.find(name);
        if (it != paramTabIndex_.end()) tabs_->setCurrentIndex(it->second);
    }
    highlightParamRow(name);
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
        // Sekmeli duzende scroll_ yoktur; editor zaten gorunur sekmede.
        if (eit != editors_.end() && scroll_)
            scroll_->ensureWidgetVisible(eit->second, 0, 16);
    }
}

} // namespace mm
