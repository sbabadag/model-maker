#include "model_maker/component_dialog_widget.hpp"
#include "model_maker/component_live_preview_widget.hpp"
#include "model_maker/component_option_icons.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QTimer>
#include <QPointer>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QGuiApplication>
#include <QIcon>
#include <QListView>
#include <QScreen>
#include <QFrame>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
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
    : QWidget(parent), def_(def),
      current_(def.typeId==L"tekla_haunch_40"
                   ?migrateHaunch40Edges(current):current) {
    // param -> hotspot id'leri (ters bağ).
    for (const auto& h : def_.schema.hotspots) {
        if (h.paramName.has_value())
            paramToHotspots_[*h.paramName].push_back(h.id);
    }

    // Dis yerlesim: [Tekla ust cubuk] + [sol sematik | sag sekmeler].
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    if (def_.schema.useTabs) outer->addWidget(buildHeaderBar());
    auto* root = new QHBoxLayout;
    root->setSpacing(8);
    outer->addLayout(root, 1);

    schematic_ = new ComponentSchematicView(def_.schema, this);
    schematic_->setMinimumWidth(300);
    if (def_.typeId == L"tekla_haunch_40") {
        // Native 40's new own 3D preview is the MAIN PAGE. The standard
        // schema-driven property editors remain authoritative on the right.
        auto* left = new QWidget(this);
        auto* vertical = new QVBoxLayout(left);
        vertical->setContentsMargins(0,0,0,0);
        vertical->setSpacing(5);
        auto* views = new QHBoxLayout;
        auto* title = new QLabel(QStringLiteral("3D Canli Onizleme"),left);
        title->setStyleSheet(QStringLiteral("font-weight:700;font-size:14px;color:#314760"));
        views->addWidget(title,1);
        for(const auto& view: {
                std::pair<const char*,const char*>{"iso","3D"},
                {"front","On"},{"side","Yan"},{"top","Ust"}}) {
            auto* button=new QPushButton(QString::fromUtf8(view.second),left);
            button->setFixedWidth(52);
            QObject::connect(button,&QPushButton::clicked,this,
                [this,key=std::string(view.first)]() {
                    if(nativePreview_)nativePreview_->setView(key);
                });
            views->addWidget(button);
        }
        vertical->addLayout(views);
        nativePreview_=new ComponentLivePreviewWidget(left);
        nativePreview_->setOnEdit([this](const std::wstring& name,double value) {
            updateFormFromPreview(name,value);
        });
        nativePreview_->setParams(current_);
        vertical->addWidget(nativePreview_,1);
        // Three REAL orthographic projections of the SAME native generator
        // plan, not separate static/embedded images.
        auto* orthos=new QHBoxLayout;
        for(const auto& view : {
              std::pair<const char*,const char*>{"front","On"},
              {"side","Yan"},{"top","Ust"}}) {
            auto* small=new ComponentLivePreviewWidget(left);
            small->setCompact(true);
            small->setFixedHeight(100);
            small->setView(view.first);
            small->setParams(current_);
            const std::string key=view.first;
            small->setOnPick([this,key]() {
                if(nativePreview_)nativePreview_->setView(key);
            });
            thumbnailViews_.push_back(small);
            orthos->addWidget(small,1);
        }
        vertical->addLayout(orthos);
        root->addWidget(left, /*stretch*/ 5);
        schematic_->hide();
    } else {
        root->addWidget(schematic_, /*stretch*/ 3);
    }

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setMinimumWidth(380);
    paramBody_ = new QWidget;
    paramLayout_ = new QVBoxLayout(paramBody_);
    paramLayout_->setContentsMargins(4, 4, 4, 4);
    paramLayout_->setSpacing(6);

    // Grup başına bir bölüm; grupta olmayan spec'ler "Diğer" bölümüne.
    std::vector<std::wstring> ungrouped;
    for (const auto& spec : def_.specs) {
        // Preserve legacy boltEdge in model records, but do not expose an
        // extra confusing common-edge field alongside editable eL/eR/eT/eB.
        if(def_.typeId==L"tekla_haunch_40" &&
           (spec.name==L"boltEdge" ||
            spec.name==L"boltCountX" || spec.name==L"boltCountY" ||
            spec.name==L"boltPitchX" || spec.name==L"boltPitchY" ||
            spec.name==L"boltDiameter" || spec.name==L"holeDiameter" ||
            spec.name==L"boltEdgeTop" || spec.name==L"boltEdgeBottom"))
            continue;
        ungrouped.push_back(spec.name);
    }

    if (def_.schema.useTabs) {
        // SEKMELI duzen (Tekla 146): her grup bir SEKME. Ayni parametre birden
        // fazla sekmede listelenebilir (or. plateThickness hem Picture hem
        // Plates) ama TEK editoru olur — ilk sekmede acilir, digeri atlanir;
        // aksi halde iki editor ayni degeri paylasmaz ve son yazan kazanir.
        tabs_ = new QTabWidget(this);
        tabs_->setMinimumWidth(380);
        int canvasTabs = 0;
        for (const auto& g : def_.schema.groups) {
            if (g.layout == L"canvas" && !g.background.empty()) {
                // Tekla'nin kendi sekmesi: resim + ayni konumda kontroller. Sabit
                // boyutludur; ekran kucukse kaydirma cubugu cikar.
                QWidget* canvas = buildCanvasPage(g, ungrouped);
                auto* canvasScroll = new QScrollArea;
                canvasScroll->setWidgetResizable(false);
                canvasScroll->setFrameShape(QFrame::NoFrame);
                canvasScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                canvasScroll->setWidget(canvas);
                QSize want(g.canvasWidth + 4, g.canvasHeight + 4);
                if (const QScreen* scr = QGuiApplication::primaryScreen()) {
                    const QRect av = scr->availableGeometry();
                    want = want.boundedTo(QSize(av.width() - 80, av.height() - 190));
                }
                canvasScroll->setMinimumSize(want);
                tabs_->addTab(canvasScroll, QString::fromStdWString(g.title));
                const int idx = tabs_->count() - 1;
                for (const auto& pn : g.paramNames)
                    if (editors_.count(pn) && !paramTabIndex_.count(pn)) paramTabIndex_[pn] = idx;
                ++canvasTabs;
                continue;
            }
            // Sayfa = [parca satirlari (t b h / Pos_No / Material...)] + [form satirlari].
            auto* page = new QWidget;
            auto* pageLay = new QVBoxLayout(page);
            pageLay->setContentsMargins(4, 4, 4, 4);
            QWidget* parts = g.partRows.empty() ? nullptr : buildPartRows(g, ungrouped);
            if (parts) pageLay->addWidget(parts);
            auto* formHost = new QWidget(page);
            auto* form = new QFormLayout(formHost);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            QWidget* custom = nullptr;
            if (g.layout == L"boxbolts") {
                custom = buildBoxBoltsPage(g, ungrouped);
                pageLay->addWidget(custom);
            } else {
                for (const auto& pn : g.paramNames) addParamRow(form, formHost, pn, ungrouped);
            }
            const bool hasContent = parts || custom || form->rowCount() > 0;
            if (!hasContent && !g.keepIfEmpty) {  // bos sekme acma
                delete page;
                continue;
            }
            if (form->rowCount() > 0) pageLay->addWidget(formHost);
            else delete formHost;
            if (!hasContent && !g.emptyNote.empty()) {
                auto* note = new QLabel(QString::fromStdWString(g.emptyNote), page);
                note->setEnabled(false);  // gri: icerigi henuz yok
                note->setWordWrap(true);
                pageLay->addWidget(note);
            }
            pageLay->addStretch(1);
            // Sekme icerigi KAYDIRILABILIR: 146 "Bolts" sekmesi 20+ satir tasir.
            auto* pageScroll = new QScrollArea;
            pageScroll->setWidgetResizable(true);
            pageScroll->setWidget(wrapWithFigures(page, g));
            tabs_->addTab(pageScroll, QString::fromStdWString(g.title));
            const int idx = tabs_->count() - 1;
            const auto mapName = [&](const std::wstring& pn) {
                if (!pn.empty() && editors_.count(pn) && !paramTabIndex_.count(pn))
                    paramTabIndex_[pn] = idx;
            };
            for (const auto& entry : g.paramNames) {
                const auto bar = entry.find(L'|');
                mapName(bar == std::wstring::npos ? entry : entry.substr(0, bar));
                if (bar != std::wstring::npos) mapName(entry.substr(bar + 1));
            }
            for (const auto& pr : g.partRows)
                for (const std::wstring* pn : {&pr.t, &pr.b, &pr.h, &pr.profile, &pr.posPrefix,
                                               &pr.posStart, &pr.asmPrefix, &pr.asmStart,
                                               &pr.material, &pr.name, &pr.finish})
                    mapName(*pn);
        }
        if (!ungrouped.empty()) {
            auto* page = new QWidget;
            auto* form = new QFormLayout(page);
            form->setLabelAlignment(Qt::AlignLeft);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            // Kopya uzerinde don: addParamRow `ungrouped`dan siler. Hicbir sekmede
            // yeri olmayan GRI (documentedOnly) alan gosterilmez — motor okumaz.
            const std::vector<std::wstring> rest = ungrouped;
            for (const auto& pn : rest) {
                const CompParamSpec* sp = specByName(def_.specs, pn);
                if (sp && sp->documentedOnly) continue;
                addParamRow(form, page, pn, ungrouped);
            }
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
        // Tum sekmeler Tekla goruntusu ise sol sematik gereksiz (Tekla'da yok).
        // (Varsa "Diğer" sekmesi yalniz Tekla'da karsiligi olmayan motor alanlarini tasir.)
        if (canvasTabs > 0) schematic_->hide();
        // Tekla "Welds...": ust cubuktaki dugmenin actigi kaynak tablosu; semadaki
        // 1..N cagrilariyla cift yonlu eslesir (satir sec -> cagri vurgulanir,
        // cagri tikla -> tablo acilir ve satir secilir).
        buildWeldTable();
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
    if(nativePreview_) {
        // Connect all existing right-side editors to the ONE native
        // param/value source. No duplicate edits or fake 'Apply' callbacks.
        for(const auto& [name, editor]:editors_) {
            if(auto* d=qobject_cast<QDoubleSpinBox*>(editor)) {
                QObject::connect(d,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                                 this,[this,name](double){
                                     releaseFixedPlateSizeOnEdgeEdit(name);
                                     updatePreviewFromForm(name);
                                 });
            } else if(auto* integer=qobject_cast<QSpinBox*>(editor)) {
                QObject::connect(integer,QOverload<int>::of(&QSpinBox::valueChanged),
                                 this,[this,name](int){updatePreviewFromForm(name);});
            } else if(auto* b=qobject_cast<QCheckBox*>(editor)) {
                QObject::connect(b,&QCheckBox::toggled,
                                 this,[this](bool){updatePreviewFromForm();});
            } else if(auto* combo=qobject_cast<QComboBox*>(editor)) {
                QObject::connect(combo,QOverload<int>::of(&QComboBox::currentIndexChanged),
                                 this,[this](int){updatePreviewFromForm();});
            } else if(auto* text=qobject_cast<QLineEdit*>(editor)) {
                QObject::connect(text,&QLineEdit::editingFinished,
                                 this,[this](){updatePreviewFromForm();});
            }
        }
    }
    if(nativePreview_) enforceHaunchInputLimits();
}

// Tek parametre satiri: [Default onay kutusu] etiket | editor.
// `optional` parametrelerde onay kutusu isaretsizse deger sonuca YAZILMAZ
// (motor varsayilani uygulanir) — Tekla'daki "Default" davranisi.
void ComponentDialogWidget::addParamRow(QFormLayout* form, QWidget* box,
                                        const std::wstring& entry,
                                        std::vector<std::wstring>& ungrouped) {
    // "a|b": ayni satirda iki editor (Tekla: Ux [Free v] [0.00]).
    std::wstring pn = entry;
    std::wstring pairName;
    if (const auto bar = entry.find(L'|'); bar != std::wstring::npos) {
        pn = entry.substr(0, bar);
        pairName = entry.substr(bar + 1);
    }
    const CompParamSpec* spec = specByName(def_.specs, pn);
    if (!spec) return;
    ungrouped.erase(std::remove(ungrouped.begin(), ungrouped.end(), pn), ungrouped.end());
    if (editors_.count(pn)) return;  // ayni parametre icin TEK editor

    const CompParam* stored = findParam(current_, pn);
    const CompParamValue value = stored ? stored->value : paramDefaultValue(*spec);
    QWidget* editor = buildEditor(*spec, value, box);
    if(editor)editor->setObjectName(QString::fromStdWString(pn));

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
        if (spec->documentedOnly) cb->setEnabled(false);  // gri alanin kutusu da gri
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

    QWidget* rowEditor = editor;
    if (!pairName.empty()) {
        if (QWidget* second = makeBareEditor(pairName, box, ungrouped)) {
            auto* hostW = new QWidget(box);
            auto* hl = new QHBoxLayout(hostW);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(6);
            hl->addWidget(editor, 1);
            hl->addWidget(second, 1);
            rowEditor = hostW;
            // Onay kutusu iki alani birlikte yonetir.
            if (const auto it = optionalChecks_.find(pn); it != optionalChecks_.end())
                optionalChecks_[pairName] = it->second;
        }
    }
    form->addRow(labelHost, rowEditor);
    editors_[pn] = editor;
    rows_[pn] = lbl;
    editor->installEventFilter(this);
}

// Tek parametre icin satirsiz/etiketsiz editor (parca satirlari kullanir).
QWidget* ComponentDialogWidget::makeBareEditor(const std::wstring& pn, QWidget* parent,
                                               std::vector<std::wstring>& ungrouped) {
    if (pn.empty()) return nullptr;
    const CompParamSpec* spec = specByName(def_.specs, pn);
    if (!spec) return nullptr;
    ungrouped.erase(std::remove(ungrouped.begin(), ungrouped.end(), pn), ungrouped.end());
    if (editors_.count(pn)) return nullptr;
    const CompParam* stored = findParam(current_, pn);
    const CompParamValue value = stored ? stored->value : paramDefaultValue(*spec);
    QWidget* editor = buildEditor(*spec, value, parent);
    if (!editor) return nullptr;
    editor->setObjectName(QString::fromStdWString(pn));
    // Dar hucre: ok dugmesiz, ek-siz.
    if (auto* d = qobject_cast<QDoubleSpinBox*>(editor)) {
        d->setSuffix(QString());
        d->setButtonSymbols(QAbstractSpinBox::NoButtons);
        d->setMinimumWidth(56);
    } else if (auto* i = qobject_cast<QSpinBox*>(editor)) {
        i->setButtonSymbols(QAbstractSpinBox::NoButtons);
        i->setMinimumWidth(40);
    } else if (auto* e = qobject_cast<QLineEdit*>(editor)) {
        e->setMinimumWidth(44);
    }
    if (spec->documentedOnly) {
        const QString hint = QStringLiteral(
            "Tekla 146 alani - henuz geometriye etkisi yok (deger kaydedilmez)");
        editor->setEnabled(false);
        editor->setToolTip(hint);
    }
    editors_[pn] = editor;
    rows_[pn] = editor;  // vurgu icin editorun kendisi
    editor->installEventFilter(this);
    return editor;
}

namespace {
// Dar Tekla kutusu (38 px) icin: gereksiz sifirlari yazmayan sayi kutusu
// ("240" / "10.25"); hassasiyet kaybi yok (decimals ayni kalir).
class CompactDoubleSpin : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double v) const override {
        return locale().toString(v, 'g', 12);
    }
};
}  // namespace

namespace {
// Acilir liste kutusu (popup) HER ZAMAN combo'nun ALTINDA ve combo'ya hizali acilir.
// Fusion/Windows stilinde Qt, secili satiri combo'nun ustune oturtur; satirlar
// buyuk resimli (70 px) oldugunda liste "kayik" gorunur. Burada acilista konum
// duzeltilir: sol kenar = combo, ust kenar = combo alti (yer yoksa ustune), genislik
// en az resim+metin kadar, ekrandan tasmaz.
class PopupAligner : public QObject {
public:
    PopupAligner(QComboBox* combo, QWidget* container) : QObject(container), combo_(combo), container_(container) {
        container->installEventFilter(this);
    }
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == container_ && ev->type() == QEvent::Show && combo_) align();
        return false;
    }
    void align() {
        QScreen* screen = combo_->screen() ? combo_->screen() : QGuiApplication::primaryScreen();
        const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
        const int w = std::max({container_->width(), combo_->width(), combo_->view()->sizeHintForColumn(0) + 24});
        int h = container_->height();
        const QPoint below = combo_->mapToGlobal(QPoint(0, combo_->height()));
        const QPoint above = combo_->mapToGlobal(QPoint(0, 0));
        int x = below.x();
        int y = below.y();
        if (y + h > avail.bottom()) {
            const int roomBelow = avail.bottom() - y;
            const int roomAbove = above.y() - avail.top();
            if (roomAbove > roomBelow) {
                h = std::min(h, roomAbove);
                y = above.y() - h;
            } else {
                h = std::min(h, roomBelow);
            }
        }
        if (x + w > avail.right()) x = std::max(avail.left(), avail.right() - w);
        container_->setGeometry(x, y, w, h);
    }

private:
    QPointer<QComboBox> combo_;
    QWidget* container_;
};
}  // namespace

// TEKLA SEKMESI (canvas): kullanicinin gercek Tekla ekran goruntusu arka plandadir;
// her kontrol, resimdeki beyaz kutunun TAM ustune (ayni piksel) yerlestirilir.
// Resimli acilir listeler (IconCombo) seffaftir: Tekla resmi gorunur kalir, tiklayinca
// secenek listesi acilir. Geometrisi olmayan (documentedOnly) ya da Tekla'da gri
// cizilmis alanlar GRI/pasiftir ve sonuca yazilmaz.
QWidget* ComponentDialogWidget::buildCanvasPage(const ComponentDialogGroup& g,
                                               std::vector<std::wstring>& ungrouped) {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("teklaCanvas"));
    page->setFixedSize(g.canvasWidth, g.canvasHeight);
    QFont f = page->font();
    f.setPointSizeF(8.0);
    page->setFont(f);
    // Aktif (kullanilabilir) alanlarin yazisi SIYAH: koyu sistem temasinda varsayilan
    // metin rengi beyaz olup beyaz Tekla kutusunda gorunmuyordu. Secili metin mavi
    // zeminle vurgulanir (aktif alanda satir/kutu degil YAZI vurgulanir).
    page->setStyleSheet(QStringLiteral(
        "QLineEdit, QAbstractSpinBox, QComboBox { color: black;"
        "  selection-background-color: #3399FF; selection-color: white; }"
        "QLineEdit, QAbstractSpinBox { background: white; border: 1px solid #ECECEC;"
        "  border-bottom: 1px solid #838383; padding: 0px 2px; margin: 0px; }"
        "QLineEdit:disabled, QAbstractSpinBox:disabled { background: #F0F0F0; color: #8C8C8C;"
        "  border: 1px solid #E0E0E0; }"
        "QComboBox { background: white; border: 1px solid #ECECEC; border-bottom: 1px solid #838383;"
        "  padding: 0px 2px; margin: 0px; }"
        "QComboBox:disabled { background: #F0F0F0; color: #8C8C8C; border: 1px solid #E0E0E0; }"
        "QComboBox QAbstractItemView { background: white; color: black; }"));

    auto* bg = new QLabel(page);
    bg->setObjectName(QStringLiteral("teklaCanvasBackground"));
    bg->setPixmap(QPixmap(QString::fromStdWString(g.background)));
    bg->setGeometry(0, 0, g.canvasWidth, g.canvasHeight);
    bg->lower();

    const QString docHint = QStringLiteral(
        "Tekla 146 alani - henuz geometriye etkisi yok (deger kaydedilmez)");
    const auto box = [](const DialogPlacedControl& c) {
        return QRect(c.x - 1, c.y - 1, c.w + 2, c.h + 2);  // beyaz alan + 1 px cerceve
    };

    // 1) Parametre kontrolleri.
    for (const auto& c : g.placed) {
        if (c.kind == PlacedControlKind::DefaultCheck) continue;
        const CompParamSpec* spec = specByName(def_.specs, c.param);
        if (!spec) continue;
        ungrouped.erase(std::remove(ungrouped.begin(), ungrouped.end(), c.param), ungrouped.end());
        if (editors_.count(c.param)) continue;  // ayni parametre icin TEK editor
        const CompParam* stored = findParam(current_, c.param);
        const CompParamValue value = stored ? stored->value : paramDefaultValue(*spec);

        QWidget* editor = nullptr;
        const bool numeric =
            spec->type == CompParamType::Length || spec->type == CompParamType::Angle;
        if (numeric && c.kind == PlacedControlKind::Combo) {
            // Tekla'da acilir liste olan sayisal alan (or. Bolt size): duzenlenebilir
            // liste; standart bulon caplari + elle deger.
            auto* combo = new QComboBox(page);
            combo->setEditable(true);
            double v = spec->defDouble;
            if (const auto* d = std::get_if<double>(&value)) v = *d;
            for (double s : {10.0, 12.0, 14.0, 16.0, 20.0, 22.0, 24.0, 27.0, 30.0, 33.0, 36.0}) {
                if (spec->hasMinDouble && s < spec->minDouble) continue;
                if (spec->hasMaxDouble && s > spec->maxDouble) continue;
                combo->addItem(QString::number(s, 'g', 12));
            }
            combo->setCurrentText(QString::number(v, 'g', 12));
            editor = combo;
        } else if (numeric) {
            auto* spin = new CompactDoubleSpin(page);
            spin->setDecimals(4);
            spin->setRange(spec->hasMinDouble ? spec->minDouble : -1e12,
                           spec->hasMaxDouble ? spec->maxDouble : 1e12);
            double v = spec->defDouble;
            if (const auto* d = std::get_if<double>(&value)) v = *d;
            spin->setValue(v);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            if (!spec->unit.empty())
                spin->setToolTip(QString::fromStdWString(spec->unit));
            editor = spin;
        } else {
            editor = buildEditor(*spec, value, page);
            if (auto* i = qobject_cast<QSpinBox*>(editor))
                i->setButtonSymbols(QAbstractSpinBox::NoButtons);
        }
        if (!editor) continue;
        editor->setObjectName(QString::fromStdWString(c.param));
        // Tekla ekran goruntusunde gri cizilmis ama bizde GERCEK (motor parametresi olan)
        // alan aktiftir: gri yalniz geometrisi olmayan (documentedOnly) alanlar icin.
        const bool grey = spec->documentedOnly;
        QRect r = box(c);
        if (c.kind == PlacedControlKind::IconCombo) {
            r = QRect(c.x, c.y, c.w, c.h);  // resmin tamami tiklanabilir
            if (auto* cb = qobject_cast<QComboBox*>(editor)) {
                // Kapali kutuda secili resim Tekla'nin arka plan resminde zaten var:
                // kutu yuzundeki simgeyi gizle, acilan listede buyuk resim kalsin.
                if (cb->property("hasOptionIcons").toBool()) {
                    cb->setIconSize(QSize(0, 0));
                    cb->view()->setIconSize(QSize(120, 70));
                }
                cb->setStyleSheet(QStringLiteral(
                    "QComboBox { background: transparent; border: none; color: transparent; }"
                    "QComboBox::drop-down { border: none; width: 0px; }"
                    "QComboBox QAbstractItemView { background: white; color: black; }"));
                // Secim Tekla'nin arka plan resminden FARKLIYSA: ayri bir yazi kutusu
                // gostermek yerine, combo'nun bulundugu alana secilen secenegin RESMI
                // (resim yoksa yazisi) cizilir; combo bunun USTUNDE seffaf kalir.
                auto* shown = new QLabel(page);
                shown->setGeometry(r);
                shown->setAlignment(Qt::AlignCenter);
                shown->setStyleSheet(QStringLiteral("background: white; color: black;"));
                shown->setAttribute(Qt::WA_TransparentForMouseEvents);
                const QString initial = cb->currentText();
                const auto refresh = [shown, cb, initial, r]() {
                    if (cb->currentText() == initial) { shown->hide(); return; }
                    const QIcon ic = cb->itemIcon(cb->currentIndex());
                    if (!ic.isNull()) {
                        shown->setPixmap(ic.pixmap(QSize(r.width() - 4, r.height() - 4)));
                    } else {
                        shown->setPixmap(QPixmap());
                        shown->setText(cb->currentText());
                    }
                    shown->show();
                };
                QObject::connect(cb, &QComboBox::currentTextChanged, page, refresh);
                refresh();
                cb->setToolTip(QString::fromStdWString(
                    spec->description.empty() ? spec->name : spec->description) +
                    QStringLiteral(": ") + cb->currentText());
            }
            if (grey && !c.greyed) {
                // Tekla'da aktif cizilmis ama bizde geometrisi yok: resmi soluklastir.
                auto* veil = new QWidget(page);
                veil->setGeometry(r);
                veil->setStyleSheet(QStringLiteral("background: rgba(240,240,240,150);"));
                veil->setAttribute(Qt::WA_TransparentForMouseEvents);
                veil->setToolTip(docHint);
            }
        } else if (qobject_cast<QCheckBox*>(editor)) {
            r = QRect(c.x - 1, c.y - 1, std::max(c.w, 13) + 3, std::max(c.h, 13) + 3);
        }
        editor->setGeometry(r);
        if (grey) {
            editor->setEnabled(false);
            editor->setToolTip(spec->documentedOnly ? docHint
                                                    : QStringLiteral("Tekla'da pasif (gri)"));
        } else if (editor->toolTip().isEmpty()) {
            editor->setToolTip(QString::fromStdWString(
                spec->description.empty() ? spec->name : spec->description));
        }
        editor->raise();
        editors_[c.param] = editor;
        rows_[c.param] = editor;  // vurgu icin editorun kendisi
        editor->installEventFilter(this);
    }

    // 2) Tekla'nin alan solundaki onay kutulari: isaretsiz = yonettigi alanlar
    //    sonuca yazilmaz (motor varsayilani). Yonettigi alanlarin hepsi griyse gri.
    for (const auto& c : g.placed) {
        if (c.kind != PlacedControlKind::DefaultCheck) continue;
        auto* cb = new QCheckBox(page);
        cb->setChecked(true);  // Tekla varsayilani: isaretli
        cb->setGeometry(c.x - 1, c.y - 1, std::max(c.w, 13) + 3, std::max(c.h, 13) + 3);
        bool anyLive = false;
        for (const auto& pn : c.governs) {
            const auto it = editors_.find(pn);
            if (it == editors_.end()) continue;
            optionalChecks_[pn] = cb;
            if (it->second->isEnabled()) anyLive = true;
        }
        cb->setEnabled(anyLive);
        cb->setToolTip(QStringLiteral("Isaretsiz = Default (motor varsayilani)"));
        cb->raise();
    }
    return page;
}

// Tekla BoxPBolts / BoxSBolts duzeni (gercek ekran goruntusu): solda cerceveli
// kutu (Bolt size/standard/Tolerance/Thread in mat/Top/Bottom) + altinda Comment;
// yaninda "Top" ve "Bottom" kutulari (oval delik olculeri, Hole type, Rotate Slots,
// Slots in, bulon boyu artisi); altta bulon grubu olculeri (1-5).
QWidget* ComponentDialogWidget::buildBoxBoltsPage(const ComponentDialogGroup& g,
                                                  std::vector<std::wstring>& ungrouped) {
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    const std::wstring& p = g.prefix;
    const auto formBox = [&](QWidget* parent, const QString& title) {
        auto* box = new QGroupBox(title, parent);
        auto* f = new QFormLayout(box);
        f->setLabelAlignment(Qt::AlignLeft);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return std::make_pair(box, f);
    };

    auto [left, leftForm] = formBox(page, QString());
    for (const wchar_t* sfx : {L"Size", L"Standard", L"Tolerance", L"ThreadInMat",
                               L"SiteTop", L"SiteBottom"})
        addParamRow(leftForm, left, p + sfx, ungrouped);
    grid->addWidget(left, 0, 0);

    for (int i = 0; i < 2; ++i) {
        const std::wstring S = i == 0 ? L"Top" : L"Bottom";
        auto [box, f] = formBox(page, QString::fromStdWString(S));
        for (const wchar_t* sfx : {L"SlotV", L"SlotH", L"HoleType", L"RotateSlots",
                                   L"SlotsIn", L"LengthIncrease"})
            addParamRow(f, box, p + S + sfx, ungrouped);
        grid->addWidget(box, 0, 1 + i);
    }

    auto [commentBox, commentForm] = formBox(page, QString());
    commentBox->setFlat(true);
    addParamRow(commentForm, commentBox, p + L"Comment", ungrouped);
    grid->addWidget(commentBox, 1, 0);

    auto [dims, dimsForm] = formBox(page, QStringLiteral("Bolt group dimensions"));
    for (const wchar_t* sfx : {L"Pos1", L"Edge", L"Count", L"Spacing", L"Pos5"})
        addParamRow(dimsForm, dims, p + sfx, ungrouped);
    grid->addWidget(dims, 2, 0, 1, 3);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setRowStretch(3, 1);
    return page;
}

// Tekla parca satirlari: [etiket] [x] t b h | [x] Pos_No | [x] Material [...] |
// [x] Name | [x] Finish. Onay kutusu = "Default degil, degeri kullan"; kapaliysa
// hucredeki parametreler sonuca YAZILMAZ. Gri (documentedOnly) hucrelerin kutusu da gri.
QWidget* ComponentDialogWidget::buildPartRows(const ComponentDialogGroup& g,
                                              std::vector<std::wstring>& ungrouped) {
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 6);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    // Sutunlar: 0 etiket | 1 x | 2 t | 3 b | 4 h | 5 x | 6 pos1 | 7 pos2 | 8 x |
    //           9 material | 10 ... | 11 x | 12 name | 13 x | 14 finish
    const auto header = [&](int col, const wchar_t* text, int span = 1) {
        auto* l = new QLabel(QString::fromStdWString(text), host);
        l->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        grid->addWidget(l, 0, col, 1, span);
    };
    bool anyTbh = false;
    for (const auto& pr : g.partRows) anyTbh = anyTbh || !pr.t.empty() || !pr.b.empty() ||
                                               !pr.h.empty() || !pr.profile.empty();
    if (anyTbh) { header(2, L"t"); header(3, L"b"); header(4, L"h"); }
    header(6, L"Pos_No", 2);
    header(9, L"Material", 2);
    header(12, L"Name");
    header(14, L"Finish");

    const auto addCell = [&](int row, int chkCol,
                             std::vector<std::pair<std::wstring, int>> cells) {
        std::vector<std::wstring> used;
        bool allDoc = true;
        for (const auto& [pn, col] : cells) {
            QWidget* ed = makeBareEditor(pn, host, ungrouped);
            if (!ed) continue;
            grid->addWidget(ed, row, col);
            used.push_back(pn);
            const CompParamSpec* sp = specByName(def_.specs, pn);
            if (sp && !sp->documentedOnly) allDoc = false;
        }
        if (used.empty()) return;
        auto* cb = new QCheckBox(host);
        cb->setChecked(true);  // Tekla varsayilani: isaretli
        cb->setToolTip(QStringLiteral("Isaretsiz = Default (motor varsayilani)"));
        if (allDoc) cb->setEnabled(false);
        for (const auto& pn : used) optionalChecks_[pn] = cb;
        grid->addWidget(cb, row, chkCol);
    };
    const auto addDots = [&](int row, int col) {
        auto* b = new QPushButton(QStringLiteral("..."), host);
        b->setFixedWidth(26);
        b->setEnabled(false);  // malzeme/profil katalogu bu uygulamada yok
        grid->addWidget(b, row, col);
    };

    int row = 1;
    for (const auto& pr : g.partRows) {
        auto* lbl = new QLabel(QString::fromStdWString(pr.label), host);
        grid->addWidget(lbl, row, 0);
        if (!pr.profile.empty()) {
            addCell(row, 1, {{pr.profile, 2}});
            if (auto* it = grid->itemAtPosition(row, 2)) {
                // profil metni t/b/h sutunlarina yayilir
                QWidget* w = it->widget();
                grid->removeWidget(w);
                grid->addWidget(w, row, 2, 1, 3);
            }
        } else {
            addCell(row, 1, {{pr.t, 2}, {pr.b, 3}, {pr.h, 4}});
        }
        addCell(row, 5, {{pr.posPrefix, 6}, {pr.posStart, 7}});
        addCell(row, 8, {{pr.material, 9}});
        if (!pr.material.empty()) addDots(row, 10);
        addCell(row, 11, {{pr.name, 12}});
        addCell(row, 13, {{pr.finish, 14}});
        ++row;
        if (!pr.asmPrefix.empty()) {  // Pos_No ikinci satir (montaj numarasi)
            addCell(row, 5, {{pr.asmPrefix, 6}, {pr.asmStart, 7}});
            ++row;
        }
    }
    grid->setColumnStretch(14, 1);
    return host;
}

// Tekla ust cubuk. Save/Load/Save as (ayar dosyasi) ve "ignore other types"
// bu uygulamada karsiligi olmadigi icin GRI; Help... Tekla sayfasini acar;
// "Welds" onay kutusu + dugme kaynak tablosuna baglidir.
QWidget* ComponentDialogWidget::buildHeaderBar() {
    auto* host = new QWidget(this);
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 4);
    v->setSpacing(4);

    auto* r1 = new QHBoxLayout;
    const auto grey = [&](QWidget* w) { w->setEnabled(false); return w; };
    r1->addWidget(grey(new QPushButton(QStringLiteral("Save"), host)));
    r1->addWidget(grey(new QPushButton(QStringLiteral("Load"), host)));
    auto* preset = new QComboBox(host);
    preset->addItem(QStringLiteral("standard"));
    preset->setMinimumWidth(180);
    r1->addWidget(grey(preset), 1);
    r1->addWidget(grey(new QPushButton(QStringLiteral("Save as"), host)));
    auto* presetName = new QLineEdit(QStringLiteral("standard"), host);
    r1->addWidget(grey(presetName), 1);
    auto* help = new QPushButton(QStringLiteral("Help..."), host);
    help->setEnabled(def_.typeId == L"tekla_shear_plate_146");
    QObject::connect(help, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://support.tekla.com/doc/tekla-structures/2026/conn_146_help")));
    });
    r1->addWidget(help);
    v->addLayout(r1);

    auto* r2 = new QHBoxLayout;
    auto* ignore = new QComboBox(host);
    ignore->addItem(QStringLiteral("ignore other types"));
    r2->addWidget(grey(ignore));
    r2->addSpacing(40);
    weldsCheck_ = new QCheckBox(host);
    weldsCheck_->setChecked(true);
    weldsCheck_->setToolTip(QStringLiteral("Isaretsiz = baglanti kaynak uretmez"));
    r2->addWidget(weldsCheck_);
    auto* weldsBtn = new QPushButton(QStringLiteral("Welds..."), host);
    weldsBtn->setEnabled(!def_.schema.weldRows.empty());
    QObject::connect(weldsBtn, &QPushButton::clicked, this, [this]() { showWeldDialog(); });
    r2->addWidget(weldsBtn);
    r2->addStretch(1);
    v->addLayout(r2);
    return host;
}

// Tekla alt cubuk: OK Apply Modify Get [x/o] Cancel .... Create
QWidget* ComponentDialogWidget::createButtonBar(QDialog* dlg, const ButtonActions& actions) {
    auto* host = new QWidget(dlg);
    auto* l = new QHBoxLayout(host);
    l->setContentsMargins(0, 4, 0, 0);
    auto* ok = new QPushButton(QStringLiteral("OK"), host);
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
    l->addWidget(ok);
    const struct { const wchar_t* name; const std::function<void()>* fn; const char* tip; } tekla[] = {
        {L"Apply", &actions.apply, "Degerleri duzenlenen baglantiya uygula (pencere acik kalir)"},
        {L"Modify", &actions.modify, "Degerleri secili ayni tip baglantilara uygula"},
        {L"Get", &actions.get, "Secili baglantinin degerlerini pencereye al"}};
    for (const auto& t : tekla) {
        auto* b = new QPushButton(QString::fromStdWString(t.name), host);
        if (*t.fn) {
            b->setToolTip(QString::fromUtf8(t.tip));
            QObject::connect(b, &QPushButton::clicked, this, [fn = *t.fn]() { fn(); });
        } else {
            b->setEnabled(false);  // karsiligi yok -> gri
        }
        l->addWidget(b);
    }
    // "x / o": tum Default onay kutularini isaretle / kaldir.
    auto* toggle = new QPushButton(QStringLiteral("\u2611 / \u2610"), host);
    toggle->setToolTip(QStringLiteral("Tum Default onay kutularini isaretle / kaldir"));
    QObject::connect(toggle, &QPushButton::clicked, this, [this]() {
        bool anyChecked = false;
        for (const auto& [name, cb] : optionalChecks_)
            if (cb->isEnabled() && cb->isChecked()) { anyChecked = true; break; }
        for (const auto& [name, cb] : optionalChecks_)
            if (cb->isEnabled()) cb->setChecked(!anyChecked);
    });
    l->addWidget(toggle);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), host);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    l->addWidget(cancel);
    l->addStretch(1);
    auto* create = new QPushButton(QStringLiteral("Create"), host);
    if (actions.create) {
        create->setToolTip(QStringLiteral("Bu ayarlarla yeni baglanti olustur (ana ve ikincil parcayi secin)"));
        QObject::connect(create, &QPushButton::clicked, this,
                         [fn = actions.create]() { fn(); });
    } else {
        create->setEnabled(false);
    }
    l->addWidget(create);
    return host;
}

// Tekla "Welds" tablosu: 12 sutun (No/Size/Type/Angle/Contour/Finish/Eff.thr./
// R.opng./Placement/Prep./Length/Pitch). No sutununda onay kutusu + numara.
// Satir secimi semadaki ayni numarali cagriyi vurgular; cagriya tiklamak satiri secer.
void ComponentDialogWidget::buildWeldTable() {
    if (def_.schema.weldRows.empty()) return;
    const int n = static_cast<int>(def_.schema.weldRows.size());
    weldDialog_ = new QDialog(this);
    weldDialog_->setWindowTitle(QStringLiteral("Welds"));
    weldDialog_->resize(900, 360);
    auto* weldLay = new QVBoxLayout(weldDialog_);
    weldTable_ = new QTableWidget(n, 12, weldDialog_);
    weldLay->addWidget(weldTable_);
    auto* closeBtn = new QPushButton(QStringLiteral("Kapat"), weldDialog_);
    QObject::connect(closeBtn, &QPushButton::clicked, weldDialog_, &QDialog::hide);
    auto* closeRow = new QHBoxLayout;
    closeRow->addStretch(1);
    closeRow->addWidget(closeBtn);
    weldLay->addLayout(closeRow);
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

    QObject::connect(weldTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!schematic_ || !weldTable_) return;
        const auto rows = weldTable_->selectionModel()->selectedRows();
        schematic_->setHighlightedWeldRow(rows.isEmpty() ? -1 : rows.first().row());
    });
    // Semadaki numarali cagri -> kaynak penceresi acilir, satir secilir.
    QObject::connect(schematic_, &ComponentSchematicView::weldCalloutClicked, this,
                     [this](int row) {
                         if (!weldTable_) return;
                         showWeldDialog();
                         weldTable_->selectRow(row);
                         if (auto* it = weldTable_->item(row, 0))
                             weldTable_->scrollToItem(it);
                     });
}

void ComponentDialogWidget::showWeldDialog() {
    if (!weldDialog_) return;
    weldDialog_->show();
    weldDialog_->raise();
    weldDialog_->activateWindow();
}

// Tablodaki duzenlenmis kaynak satirlarini geri okur (kaydetmek isteyen cagiran icin).
std::vector<WeldTableRow> ComponentDialogWidget::resultWeldRows() const {
    std::vector<WeldTableRow> out = def_.schema.weldRows;
    if (!weldTable_) return out;
    // Ust cubuktaki "Welds" onay kutusu kapaliysa hicbir kaynak uretilmez.
    const bool weldsOn = !weldsCheck_ || weldsCheck_->isChecked();
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
        out[r].enabled = (no ? no->checkState() == Qt::Checked : out[r].enabled) && weldsOn;
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
            // Tekla yardim sayfasindaki secenek resimleri: her satir "resim + ad".
            const auto* icons = optionIconsFor(spec.name);
            bool anyIcon = false;
            for (std::size_t ci = 0; ci < spec.choices.size(); ++ci) {
                const QString text = QString::fromStdWString(spec.choices[ci]);
                if (icons && ci < icons->size() && !(*icons)[ci].empty()) {
                    const QIcon ic{QString::fromStdWString((*icons)[ci])};
                    if (!ic.isNull()) {
                        combo->addItem(ic, text);
                        anyIcon = true;
                        continue;
                    }
                }
                combo->addItem(text);
            }
            if (anyIcon) {
                auto* view = new QListView(combo);
                view->setUniformItemSizes(false);
                combo->setView(view);
                combo->setIconSize(QSize(36, 22));          // kapali kutudaki kucuk resim
                combo->view()->setIconSize(QSize(120, 70)); // acilan listede buyuk resim
                new PopupAligner(combo, combo->view()->window());
                combo->setProperty("hasOptionIcons", true);
            }
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

void ComponentDialogWidget::setEditorValue(QWidget* editor, const CompParamSpec& spec,
                                           const CompParamValue& value) {
    if (!editor) return;
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle: {
            double v = spec.defDouble;
            if (const auto* d = std::get_if<double>(&value)) v = *d;
            if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                combo->setEditText(QString::number(v, 'g', 12));
            } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
                spin->setValue(v);
            }
            break;
        }
        case CompParamType::Integer: {
            int v = spec.defInt;
            if (const auto* n = std::get_if<int>(&value)) v = *n;
            if (auto* spin = qobject_cast<QSpinBox*>(editor)) spin->setValue(v);
            break;
        }
        case CompParamType::Boolean: {
            bool v = spec.defBool;
            if (const auto* b = std::get_if<bool>(&value)) v = *b;
            if (auto* box = qobject_cast<QCheckBox*>(editor)) box->setChecked(v);
            break;
        }
        case CompParamType::Enum: {
            std::wstring v = spec.defString;
            if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
            if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                const int idx = combo->findText(QString::fromStdWString(v));
                if (idx >= 0) combo->setCurrentIndex(idx);
            }
            break;
        }
        case CompParamType::EntityRef:
            break;  // salt-okunur
        case CompParamType::String:
        case CompParamType::Profile:
        case CompParamType::Material:
        default: {
            std::wstring v = spec.defString;
            if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
            if (auto* edit = qobject_cast<QLineEdit*>(editor))
                edit->setText(QString::fromStdWString(v));
            break;
        }
    }
}

void ComponentDialogWidget::setPreviewInput(const ComponentInput& input) {
    if(nativePreview_) {
        haunchBeamHeight_=0.0;
        haunchBeamWidth_=0.0;
        if(!input.secondary.empty()) {
            const auto verticality=[](const MemberGeometry& m) {
                const Vec3 d=m.axisTo-m.axisFrom;
                const double len=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
                return len>1e-9?std::fabs(d.z)/len:0.0;
            };
            const bool swapped=verticality(input.main)+1e-6<
                               verticality(input.secondary.front());
            haunchBeamHeight_=swapped?input.main.secHeight:
                                     input.secondary.front().secHeight;
            haunchBeamWidth_=swapped?input.main.secWidth:
                                    input.secondary.front().secWidth;
        }
        enforceHaunchInputLimits();
        nativePreview_->setInput(input);
        const auto params=resultParams();
        nativePreview_->setParams(params);
        for(auto* view:thumbnailViews_) {
            view->setInput(input);
            view->setParams(params);
        }
    }
}
void ComponentDialogWidget::releaseFixedPlateSizeOnEdgeEdit(
    const std::wstring& name) {
    if(def_.typeId!=L"tekla_haunch_40")return;
    // A manual width and two independent horizontal edge distances
    // overconstrain the layout, so editing eL/eR selects auto width.
    // Vertical edges are DIFFERENT: keep manually set plate height and
    // move the upper/lower bolt grids within its boundaries.
    if(name!=L"boltEdgeLeft" && name!=L"boltEdgeRight")return;
    const wchar_t* key=L"endPlateWidth";
    const auto it=editors_.find(key);
    if(it==editors_.end())return;
    if(auto* d=qobject_cast<QDoubleSpinBox*>(it->second)) {
        QSignalBlocker guard(d);
        if(d->value()>0.0)d->setValue(0.0);
    }
}
void ComponentDialogWidget::enforceHaunchInputLimits(const std::wstring& changed) {
    if(def_.typeId!=L"tekla_haunch_40")return;
    auto dbl=[this](const std::wstring& k)->QDoubleSpinBox* {
        const auto it=editors_.find(k);
        return it==editors_.end()?nullptr:qobject_cast<QDoubleSpinBox*>(it->second);
    };
    auto integer=[this](const std::wstring& k)->QSpinBox* {
        const auto it=editors_.find(k);
        return it==editors_.end()?nullptr:qobject_cast<QSpinBox*>(it->second);
    };
    auto value=[&](const std::wstring& k,double fallback)->double {
        if(auto* s=dbl(k))return s->value();
        if(auto* s=integer(k))return s->value();
        return fallback;
    };
    auto bounds=[&](const std::wstring& key,double minimum,double maximum) {
        if(auto* s=dbl(key)) {
            QSignalBlocker block(s);
            s->setRange(minimum,std::max(minimum,maximum));
        }
    };
    auto minExplicit=[&](const std::wstring& key,double minimum) {
        if(auto* s=dbl(key)) {
            QSignalBlocker block(s);
            if(s->value()>0.0 && s->value()<minimum)s->setValue(minimum);
        }
    };
    const double legacy=std::clamp(paramAsDouble(current_,L"boltEdge",35.0),
                                   20.0,120.0);
    const double boltMin=12.0;
    const double edgeMin=10.0;
    const double tip=value(L"haunchTipDepth",25.0);
    bounds(L"haunchDepth",std::max(60.0,tip+25.01),1000.0);
    const double depth=value(L"haunchDepth",240.0);
    bounds(L"haunchTipDepth",10.0,std::min(200.0,depth-25.01));
    struct Group {
        std::wstring prefix;
        int nx{2},ny{2};
        double px{120.0},py{80.0},hole{22.0},diameter{20.0},clear{44.0};
        double spanX() const {return (nx-1)*px;}
        double spanY() const {return (ny-1)*py;}
    };
    auto read=[&](const wchar_t* prefix) {
        Group g;g.prefix=prefix;
        g.nx=static_cast<int>(value(g.prefix+L"BoltCountX",2.0));
        g.ny=static_cast<int>(value(g.prefix+L"BoltCountY",2.0));
        g.px=value(g.prefix+L"BoltPitchX",120.0);
        g.py=value(g.prefix+L"BoltPitchY",80.0);
        g.diameter=value(g.prefix+L"BoltDiameter",20.0);
        g.hole=value(g.prefix+L"HoleDiameter",22.0);
        g.clear=value(g.prefix+L"BoltEdgeClear",44.0);
        return g;
    };
    // Keep physical hole diameter coupled only to its OWN group's bolt,
    // including when sizes are edited directly in the 3D mockup.
    for(const wchar_t* groupName:{L"upper",L"lower"}) {
        const std::wstring prefix(groupName);
        bounds(prefix+L"BoltDiameter",boltMin,36.0);
        const double bolt=value(prefix+L"BoltDiameter",20.0);
        bounds(prefix+L"HoleDiameter",std::max(13.0,bolt+2.0),50.0);
        const double hole=value(prefix+L"HoleDiameter",22.0);
        bounds(prefix+L"BoltPitchX",std::max(40.0,hole+0.01),500.0);
        bounds(prefix+L"BoltPitchY",std::max(40.0,hole+0.01),500.0);
        bounds(prefix+L"BoltEdgeClear",edgeMin,190.0);
    }
    Group upper=read(L"upper"),lower=read(L"lower");
    const double autoH=haunchBeamHeight_>0.0
        ?haunchBeamHeight_+depth+2.0*legacy:0.0;
    double height=value(L"endPlateHeight",0.0);
    const bool manual=height>0.0;
    if(!manual)height=autoH;
    auto minHeight=[&](const Group& u,const Group& l) {
        return u.clear+l.clear+u.hole+l.hole+
               u.spanY()+l.spanY()+0.20;
    };
    // When the user changes the plate height, clamp the plate (not the
    // bolts) at the true physical minimum for two independent grids.
    if(manual && (changed.empty() || changed==L"endPlateHeight")) {
        minExplicit(L"endPlateHeight",minHeight(upper,lower));
        height=value(L"endPlateHeight",height);
    }
    const auto fits=[&](const Group& u,const Group& l) {
        return height<=0.0 || minHeight(u,l)<=height+1e-7;
    };
    // The edited group receives the limits. The opposite group's data is
    // NEVER clamped while editing this one.
    auto clampChangedGroup=[&](Group& current,const Group& other) {
        if(!changed.starts_with(current.prefix))return;
        const std::wstring key=current.prefix+L"BoltEdgeClear";
        if(changed==key) {
            const double occupied=other.clear+current.hole+other.hole+
                                 current.spanY()+other.spanY()+0.20;
            bounds(key,edgeMin,std::min(190.0,
                   std::max(edgeMin,height-occupied)));
        } else if(changed==current.prefix+L"BoltPitchY" ||
                  changed==current.prefix+L"BoltCountY") {
            const double occupied=current.clear+other.clear+
                                  current.hole+other.hole+other.spanY()+0.20;
            const double available=std::max(0.0,height-occupied);
            const int maxRows=std::clamp(1+
               static_cast<int>(std::floor(available/
                                    std::max(40.0,current.py))),1,4);
            if(auto* s=integer(current.prefix+L"BoltCountY")) {
                QSignalBlocker block(s);
                s->setRange(1,maxRows);
            }
            const int rows=static_cast<int>(
                value(current.prefix+L"BoltCountY",2.0));
            if(rows>1)
                bounds(current.prefix+L"BoltPitchY",
                       std::max(40.0,current.hole+0.01),
                       std::min(500.0,std::max(40.0,available/(rows-1))));
        }
        current=read(current.prefix.c_str());
    };
    if(height>0.0) {
        if(changed.starts_with(L"lower"))clampChangedGroup(lower,upper);
        else if(changed.starts_with(L"upper"))clampChangedGroup(upper,lower);
        // A new bolt/hole diameter may require more room even at the
        // same counts. Prefer to preserve the other group and plate size.
        if(manual && !fits(upper,lower) &&
           (changed.empty() || changed==L"endPlateHeight")) {
            minExplicit(L"endPlateHeight",minHeight(upper,lower));
        }
    }
    const double holeMax=std::max(upper.hole,lower.hole);
    const double minSide=std::max(20.0,holeMax*0.5+0.20);
    bounds(L"boltEdgeLeft",minSide,200.0);
    bounds(L"boltEdgeRight",minSide,200.0);
    const double left=value(L"boltEdgeLeft",35.0);
    const double right=value(L"boltEdgeRight",35.0);
    const double spanX=std::max(upper.spanX(),lower.spanX());
    const double minW=std::max({
        spanX+2.0*minSide,
        upper.spanX()+upper.hole+15.0,
        lower.spanX()+lower.hole+15.0
    });
    const double fixedW=value(L"endPlateWidth",0.0);
    if(fixedW>0.0) {
        minExplicit(L"endPlateWidth",minW);
        const double achieved=(value(L"endPlateWidth",minW)-spanX)*0.5;
        // Legacy fixed-width grid is centred; shared horizontal side
        // controls describe achieved spacing without moving plate here.
        for(const wchar_t* key:{L"boltEdgeLeft",L"boltEdgeRight"})
            if(auto* s=dbl(key)) {
                QSignalBlocker block(s);
                s->setValue(achieved);
            }
    } else if(haunchBeamWidth_>0.0) {
        const double required=haunchBeamWidth_+20.0-spanX-left;
        if(required<=200.0)
            bounds(L"boltEdgeRight",std::max(minSide,required),200.0);
    }
    const double flangeWidth=value(L"haunchFlangeWidth",0.0);
    if(flangeWidth>0.0)
        minExplicit(L"haunchFlangeWidth",value(L"haunchWebThickness",10.0));
}
void ComponentDialogWidget::updatePreviewFromForm(const std::wstring& changed) {
    if(nativePreview_) {
        enforceHaunchInputLimits(changed);
        const auto params=resultParams();
        nativePreview_->setParams(params);
        for(auto* view:thumbnailViews_)view->setParams(params);
    }
}
void ComponentDialogWidget::updateFormFromPreview(const std::wstring& name,
                                                   double value) {
    const auto it=editors_.find(name);
    if(it==editors_.end())return;
    if(auto* d=qobject_cast<QDoubleSpinBox*>(it->second)) {
        const QSignalBlocker guard(d);
        d->setValue(value);
    } else if(auto* integer=qobject_cast<QSpinBox*>(it->second)) {
        const QSignalBlocker guard(integer);
        integer->setValue(static_cast<int>(std::lround(value)));
    }
    if(const auto ck=optionalChecks_.find(name);ck!=optionalChecks_.end()) {
        const QSignalBlocker guard(ck->second);
        ck->second->setChecked(true);
    }
    releaseFixedPlateSizeOnEdgeEdit(name);
    onHotspotClicked(QString::fromStdWString(name));
    updatePreviewFromForm(name);
}

void ComponentDialogWidget::setParams(const std::vector<CompParam>& p) {
    current_ = def_.typeId==L"tekla_haunch_40"
        ?migrateHaunch40Edges(p):p;
    // Reset prior dynamic limits BEFORE loading Get/another component.
    // Otherwise a hole constrained by the previous M36 could silently
    // rewrite the incoming valid M20/22 values to 38 during setValue.
    if(def_.typeId==L"tekla_haunch_40") {
        for(const auto& spec:def_.specs) {
            const auto it=editors_.find(spec.name);
            if(it==editors_.end())continue;
            if(auto* d=qobject_cast<QDoubleSpinBox*>(it->second)) {
                QSignalBlocker guard(d);
                d->setRange(spec.hasMinDouble?spec.minDouble:-1e12,
                            spec.hasMaxDouble?spec.maxDouble:1e12);
            } else if(auto* i=qobject_cast<QSpinBox*>(it->second)) {
                QSignalBlocker guard(i);
                i->setRange(spec.hasMinInt?spec.minInt:1,
                            spec.hasMaxInt?spec.maxInt:4);
            }
        }
    }
    for (const auto& spec : def_.specs) {
        const auto it = editors_.find(spec.name);
        if (it == editors_.end() || spec.documentedOnly) continue;
        const CompParam* stored = findParam(current_, spec.name);
        {
            QSignalBlocker guard(it->second);
            setEditorValue(it->second, spec, stored ? stored->value : paramDefaultValue(spec));
        }
        const auto oc = optionalChecks_.find(spec.name);
        if (oc != optionalChecks_.end() && oc->second->isEnabled()) {
            QSignalBlocker guard(oc->second);
            oc->second->setChecked(stored != nullptr);
        }
    }
    updatePreviewFromForm();
}

CompParamValue ComponentDialogWidget::valueFromEditor(QWidget* editor,
                                                      const CompParamSpec& spec) {
    if (!editor) return paramDefaultValue(spec);
    switch (spec.type) {
        case CompParamType::Length:
        case CompParamType::Angle: {
            // Tekla sekmesinde acilir liste olan sayisal alan (Bolt size).
            if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                bool ok = false;
                double v = combo->currentText().trimmed().replace(QLatin1Char(','),
                                                                   QLatin1Char('.')).toDouble(&ok);
                if (!ok) v = spec.defDouble;
                if (spec.hasMinDouble) v = std::max(v, spec.minDouble);
                if (spec.hasMaxDouble) v = std::min(v, spec.maxDouble);
                return v;
            }
            return static_cast<QDoubleSpinBox*>(editor)->value();
        }
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
                // Odaklanan metin alaninin YAZISI secili (vurgulu) olur.
                if (event->type() == QEvent::FocusIn && editor->isEnabled()) {
                    QPointer<QWidget> guard(editor);
                    QTimer::singleShot(0, editor, [guard]() {
                        if (!guard) return;
                        if (auto* le = qobject_cast<QLineEdit*>(guard.data())) le->selectAll();
                        else if (auto* sp = qobject_cast<QAbstractSpinBox*>(guard.data()))
                            sp->selectAll();
                        else if (auto* cb = qobject_cast<QComboBox*>(guard.data()))
                            if (cb->isEditable() && cb->lineEdit()) cb->lineEdit()->selectAll();
                    });
                }
                highlightParamRow(name);
                schematic_->setHighlightedParam(name);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

namespace {
// Aktif alan vurgusu: kutu/satir ARKA PLANI degismez; yalniz yazi mavi + kalin olur.
// Resimli (IconCombo) kutular kendi saydam stilini tasir -> dokunulmaz.
const char* kHighlightSheet =
    "QLabel, QLineEdit, QAbstractSpinBox, QComboBox { color: #0B57D0; font-weight: bold; }";
}  // namespace

void ComponentDialogWidget::highlightParamRow(const std::wstring& paramName) {
    if(nativePreview_)nativePreview_->setHighlightedParam(paramName);
    if (!highlightedRow_.empty()) {
        const auto it = rows_.find(highlightedRow_);
        if (it != rows_.end() && it->second->styleSheet() == QLatin1String(kHighlightSheet))
            it->second->setStyleSheet(QString());
    }
    highlightedRow_ = paramName;
    if (paramName.empty()) return;
    const auto it = rows_.find(paramName);
    if (it != rows_.end()) {
        if (it->second->styleSheet().isEmpty() && it->second->isEnabled())
            it->second->setStyleSheet(QLatin1String(kHighlightSheet));
        const auto eit = editors_.find(paramName);
        // Sekmeli duzende scroll_ yoktur; editor zaten gorunur sekmede.
        if (eit != editors_.end() && scroll_)
            scroll_->ensureWidgetVisible(eit->second, 0, 16);
    }
}

} // namespace mm
