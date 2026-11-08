#include "model_maker/profile_picker_dialog.hpp"

#include "model_maker/profile_database.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>

namespace mm {

namespace {

// Aile adi: adin ilk rakamina kadarki harf kumesi (HEB240 -> HEB,
// CHS100 -> CHS, RHS100*50 -> RHS, D100 -> D). Harf yoksa "Diğer".
QString familyOf(const QString& name) {
    QString family;
    for (const QChar c : name) {
        if (c.isDigit()) break;
        if (c.isLetter()) family.append(c.toUpper());
    }
    return family.isEmpty() ? QStringLiteral("Diğer") : family;
}

// Glob (`*` = herhangi, `?` = tek karakter) -> regex; buyuk/kucuk harf
// duyarsiz. Yildiz yoksa tam eslesme (case-insensitive) istenir.
QRegularExpression globToRegex(const QString& glob) {
    QString pattern;
    for (const QChar c : glob) {
        if (c == QLatin1Char('*'))
            pattern.append(QStringLiteral(".*"));
        else if (c == QLatin1Char('?'))
            pattern.append(QLatin1Char('.'));
        else
            pattern.append(QRegularExpression::escape(QString(c)));
    }
    return QRegularExpression(pattern,
                              QRegularExpression::CaseInsensitiveOption);
}

bool matchesFilter(const QString& name, const QString& filter) {
    const QString trimmed = filter.trimmed();
    if (trimmed.isEmpty()) return true;
    return globToRegex(trimmed).match(name).hasMatch();
}

// ---------------------------------------------------------------------------
// SectionPreviewWidget — KENDI sematik motorumuz (UI-4): kesit resmi,
// olcu harfleri h b tw tf r (ve yuvarlakta D). QPainter ile cizilir;
// disariya dosya/resim bagimliligi YOK.
// ---------------------------------------------------------------------------
class SectionPreviewWidget : public QWidget {
public:
    explicit SectionPreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumSize(220, 170);
    }

    void setProfile(const SteelProfile* profile) {
        profile_ = profile;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(QStringLiteral("#23262D")));

        if (!profile_) return;

        const QColor steel(QStringLiteral("#E8EBF0"));
        const QColor dim(QStringLiteral("#7FA3D9"));
        const QFont small = painter.font();
        const QFont labelFont = [&small] {
            QFont f = small;
            f.setPointSizeF(f.pointSizeF() * 0.95);
            return f;
        }();
        painter.setFont(labelFont);

        const double h = profile_->height;
        const double b = profile_->width;
        const double tw = profile_->plateThickness;
        const double tf = profile_->flangeThickness;
        const double r = profile_->roundingRadius;

        const double leftMargin = 52.0;   // h olcusu icin sol pay
        const double topMargin = 46.0;    // b olcusu icin ust pay
        const double rightMargin = 24.0;
        const double bottomMargin = 20.0;
        const double availW = std::max(10.0, width() - leftMargin - rightMargin);
        const double availH = std::max(10.0, height() - topMargin - bottomMargin);
        const double scale =
            std::min(availW / std::max(b, 1.0), availH / std::max(h, 1.0));

        const double ox = leftMargin + (availW - b * scale) / 2.0;
        const double oy = topMargin + (availH - h * scale) / 2.0;

        const auto sx = [&](double x) { return ox + x * scale; };
        const auto sy = [&](double y) { return oy + y * scale; };

        const ProfileSectionKind kind = classifyProfileSection(*profile_);
        QPainterPath section;
        if (kind == ProfileSectionKind::Round) {
            const double d = std::max(h, b);
            const double rOut = d / 2.0;
            const double cx = ox + (b * scale) / 2.0;
            const double cy = oy + (h * scale) / 2.0;
            section.addEllipse(QPointF(cx, cy), rOut * scale, rOut * scale);
            if (tw > 0.0 && tw < d) {
                const double rIn = rOut - tw;
                QPainterPath inner;
                inner.addEllipse(QPointF(cx, cy), rIn * scale, rIn * scale);
                painter.setPen(QPen(steel, 1.4));
                painter.drawPath(section);
                painter.setPen(QPen(steel, 1.0, Qt::DashLine));
                painter.drawPath(inner);
                // D olcusu (yatay, merkezden)
                drawDimLine(painter, dim, QPointF(sx(0.0), cy),
                            QPointF(sx(d), cy), QStringLiteral("D"));
                // tw olcusu (sag ustte radyal kisa cizgi)
                const QPointF mid(sx(d) / 1.0, cy - rOut * scale * 0.7);
                drawDimLine(painter, dim,
                            QPointF(cx + rIn * scale, cy - rOut * scale * 0.7),
                            QPointF(cx + rOut * scale, cy - rOut * scale * 0.7),
                            QStringLiteral("tw"));
            } else {
                painter.setPen(QPen(steel, 1.4));
                painter.drawPath(section);
                const double d = std::max(h, b);
                drawDimLine(painter, dim, QPointF(sx(0.0), oy + (h * scale) / 2.0),
                            QPointF(sx(d), oy + (h * scale) / 2.0),
                            QStringLiteral("D"));
            }
            return;
        }

        // ISection: dis kontur (I) — ust flans, govde, alt flans.
        if (kind == ProfileSectionKind::ISection) {
            const double bw = b;
            const double xIn = (bw - tw) / 2.0;
            section.moveTo(sx(0.0), sy(0.0));
            section.lineTo(sx(bw), sy(0.0));
            section.lineTo(sx(bw), sy(tf));
            section.lineTo(sx(xIn + tw), sy(tf));
            section.lineTo(sx(xIn + tw), sy(h - tf));
            section.lineTo(sx(bw), sy(h - tf));
            section.lineTo(sx(bw), sy(h));
            section.lineTo(sx(0.0), sy(h));
            section.lineTo(sx(0.0), sy(h - tf));
            section.lineTo(sx(xIn), sy(h - tf));
            section.lineTo(sx(xIn), sy(tf));
            section.lineTo(sx(0.0), sy(tf));
            section.closeSubpath();
        } else {
            // Box: dis + ic dikdortgen (bosluk).
            const QRectF outer(QPointF(sx(0.0), sy(0.0)),
                               QPointF(sx(b), sy(h)));
            section.addRect(outer);
        }

        painter.setPen(QPen(steel, 1.4));
        painter.drawPath(section);
        if (kind == ProfileSectionKind::Box && tw > 0.0 && tw * 2.0 < h &&
            tw * 2.0 < b) {
            QPainterPath inner;
            inner.addRect(QRectF(QPointF(sx(tw), sy(tw)),
                                 QPointF(sx(b - tw), sy(h - tw))));
            painter.setPen(QPen(steel, 1.0, Qt::DashLine));
            painter.drawPath(inner);
        }

        // Olcu cizgileri + harfler.
        if (kind == ProfileSectionKind::ISection) {
            drawDimLine(painter, dim, QPointF(sx(0.0), sy(0.0)),
                        QPointF(sx(0.0), sy(h)), QStringLiteral("h"));
            drawDimLine(painter, dim, QPointF(sx(0.0), sy(0.0)),
                        QPointF(sx(b), sy(0.0)), QStringLiteral("b"));
            if (tw > 0.0) {
                const double xIn = (b - tw) / 2.0;
                drawDimLine(painter, dim, QPointF(sx(xIn), sy(h / 2.0)),
                            QPointF(sx(xIn + tw), sy(h / 2.0)),
                            QStringLiteral("tw"));
            }
            if (tf > 0.0) {
                drawDimLine(painter, dim, QPointF(sx(b / 2.0), sy(0.0)),
                            QPointF(sx(b / 2.0), sy(tf)), QStringLiteral("tf"));
            }
            if (r > 0.0) {
                const double xIn = (b - tw) / 2.0;
                drawDimLine(painter, dim, QPointF(sx(xIn), sy(tf + r)),
                            QPointF(sx(xIn - r), sy(tf)), QStringLiteral("r"));
            }
        } else if (kind == ProfileSectionKind::Box) {
            drawDimLine(painter, dim, QPointF(sx(0.0), sy(0.0)),
                        QPointF(sx(0.0), sy(h)), QStringLiteral("h"));
            drawDimLine(painter, dim, QPointF(sx(0.0), sy(0.0)),
                        QPointF(sx(b), sy(0.0)), QStringLiteral("b"));
            if (tw > 0.0) {
                drawDimLine(painter, dim, QPointF(sx(tw), sy(h / 2.0)),
                            QPointF(sx(0.0), sy(h / 2.0)),
                            QStringLiteral("tw"));
            }
        }
    }

private:
    static void drawDimLine(QPainter& painter, const QColor& color,
                            QPointF a, QPointF b, const QString& label) {
        painter.setPen(QPen(color, 0.9));
        painter.drawLine(a, b);
        // Ok uclari (kucuk kene)
        const QPointF d = b - a;
        const double len = std::sqrt(QPointF::dotProduct(d, d));
        if (len < 1.0) return;
        const QPointF u = d / len;
        const QPointF n(-u.y(), u.x());
        const double tick = 4.0;
        painter.drawLine(a, a + u * tick + n * tick);
        painter.drawLine(a, a + u * tick - n * tick);
        painter.drawLine(b, b - u * tick + n * tick);
        painter.drawLine(b, b - u * tick - n * tick);
        // Etiket: dogruya gore hizala.
        const QPointF mid = (a + b) / 2.0;
        painter.save();
        painter.translate(mid);
        const double angle = std::atan2(d.y(), d.x()) * 180.0 / 3.141592653589793;
        if (angle > 90.0 || angle < -90.0) painter.rotate(180.0);
        QFont f = painter.font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        painter.setFont(f);
        QRectF textRect(-80.0, -18.0, 160.0, 14.0);
        painter.drawText(textRect, Qt::AlignCenter, label);
        painter.restore();
    }

    const SteelProfile* profile_{};
};

// Sayi bicimi: tam sayi ise noktasiz, degilse 3 anlamli basamak.
QString formatValue(double v) {
    if (v == 0.0) return QStringLiteral("-");
    if (std::abs(v - std::round(v)) < 1e-9)
        return QString::number(static_cast<qint64>(std::llround(v)));
    return QString::number(v, 'g', 4);
}

} // namespace

// ---------------------------------------------------------------------------
// ProfilePickerDialog
// ---------------------------------------------------------------------------
ProfilePickerDialog::ProfilePickerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Profil Seçimi"));
    resize(860, 560);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(6);

    // ---- Ust: filtre + profil adi ----
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);
    topRow->addWidget(new QLabel(QStringLiteral("Filtre:"), this));
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setObjectName(QStringLiteral("profileFilter"));
    filterEdit_->setPlaceholderText(QStringLiteral("* joker, ör. HE*3*"));
    topRow->addWidget(filterEdit_, 1);
    topRow->addWidget(new QLabel(QStringLiteral("Profil:"), this));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("profileName"));
    nameEdit_->setPlaceholderText(QStringLiteral("HEB240..."));
    topRow->addWidget(nameEdit_, 1);
    outer->addLayout(topRow);

    // ---- Orta: sol agac + sag detay ----
    auto* middle = new QHBoxLayout();
    middle->setSpacing(8);

    auto* leftBox = new QVBoxLayout();
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("profileTree"));
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    leftBox->addWidget(tree_, 1);
    showAllCheck_ = new QCheckBox(QStringLiteral("Tüm profilleri göster"), this);
    showAllCheck_->setChecked(true);
    leftBox->addWidget(showAllCheck_);
    middle->addLayout(leftBox, 3);

    auto* rightBox = new QVBoxLayout();
    rightBox->setSpacing(6);
    sectionPreview_ = new SectionPreviewWidget(this);
    sectionPreview_->setObjectName(QStringLiteral("profileSectionPreview"));
    sectionPreview_->setMinimumWidth(240);
    rightBox->addWidget(sectionPreview_, 1);
    sectionLabel_ = new QLabel(this);
    sectionLabel_->setObjectName(QStringLiteral("profileSectionLabel"));
    sectionLabel_->setStyleSheet(
        QStringLiteral("color: #D8DCE4; font-weight: 700;"));
    sectionLabel_->setAlignment(Qt::AlignCenter);
    rightBox->addWidget(sectionLabel_);
    propsTable_ = new QTableWidget(this);
    propsTable_->setObjectName(QStringLiteral("profilePropsTable"));
    propsTable_->setColumnCount(4);
    propsTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Özellik"), QStringLiteral("Sembol"),
         QStringLiteral("Değer"), QStringLiteral("Birim")});
    propsTable_->horizontalHeader()->setStretchLastSection(true);
    propsTable_->verticalHeader()->setVisible(false);
    propsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    propsTable_->setSelectionMode(QAbstractItemView::NoSelection);
    rightBox->addWidget(propsTable_, 2);
    showDetailsCheck_ = new QCheckBox(QStringLiteral("Ayrıntıları göster"), this);
    showDetailsCheck_->setChecked(true);
    rightBox->addWidget(showDetailsCheck_);
    middle->addLayout(rightBox, 2);
    outer->addLayout(middle, 1);

    // ---- Alt: dugmeler ----
    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    okButton_ = new QPushButton(QStringLiteral("Tamam"), this);
    okButton_->setObjectName(QStringLiteral("profileOkButton"));
    okButton_->setDefault(true);
    applyButton_ = new QPushButton(QStringLiteral("Uygula"), this);
    applyButton_->setObjectName(QStringLiteral("profileApplyButton"));
    cancelButton_ = new QPushButton(QStringLiteral("İptal"), this);
    cancelButton_->setObjectName(QStringLiteral("profileCancelButton"));
    bottom->addWidget(okButton_);
    bottom->addWidget(applyButton_);
    bottom->addWidget(cancelButton_);
    outer->addLayout(bottom);

    connect(filterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                // Kullanici filtre yazmaya basladi: "Tum profilleri goster"
                // kalkar (filtre etkinlesir).
                if (!text.isEmpty() && showAllCheck_->isChecked())
                    showAllCheck_->setChecked(false);
                applyFilter();
            });
    connect(showAllCheck_, &QCheckBox::toggled, this, [this](bool on) {
        // Tutarli durum: "Tum profilleri goster" ⇔ filtre bos.
        if (on) filterEdit_->clear();
        applyFilter();
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (suppressSync_ || !current || !current->parent()) return;
                syncFromTree();
            });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item || !item->parent()) return;
                onOkClicked();
            });
    connect(nameEdit_, &QLineEdit::textEdited, this,
            [this](const QString&) { syncToTree(); });
    connect(nameEdit_, &QLineEdit::returnPressed, this, [this]() {
        syncToTree();
        onOkClicked();
    });
    connect(showDetailsCheck_, &QCheckBox::toggled, this,
            [this](bool on) {
                sectionPreview_->setVisible(on);
                sectionLabel_->setVisible(on);
                propsTable_->setVisible(on);
            });
    connect(okButton_, &QPushButton::clicked, this,
            &ProfilePickerDialog::onOkClicked);
    connect(applyButton_, &QPushButton::clicked, this,
            &ProfilePickerDialog::onApplyClicked);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    nameEdit_->installEventFilter(this);
    tree_->installEventFilter(this);
}

bool ProfilePickerDialog::eventFilter(QObject* watched, QEvent* event) {
    // Agac: Enter/Tek tık davranisi Qt varsayilaninda; yalnizca Escape
    // iptali ve agac klavye gezinmesi kalir. Burada ekstra is yok — ancak
    // filtre alanina hizli odak icin Ctrl+F kabul edilir.
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ProfilePickerDialog::setProfiles(const std::vector<SteelProfile>& profiles) {
    profiles_ = profiles;
    std::sort(profiles_.begin(), profiles_.end(),
              [](const SteelProfile& a, const SteelProfile& b) {
                  return profileNameLess(a.name, b.name);
              });
    // Ada gore indeks (canonical arama): dupe yok (parser eler).
    rebuildTree();
}

void ProfilePickerDialog::rebuildTree() {
    const bool treeWasBlocked = tree_->signalsBlocked();
    tree_->blockSignals(true);
    tree_->clear();
    QStringList families;
    for (const auto& profile : profiles_)
        if (!families.contains(familyOf(QString::fromStdString(profile.name))))
            families << familyOf(QString::fromStdString(profile.name));
    families.sort(Qt::CaseInsensitive);
    for (const QString& family : families) {
        auto* familyItem = new QTreeWidgetItem(tree_);
        familyItem->setText(0, family);
        familyItem->setFlags(Qt::ItemIsEnabled);
        for (const auto& profile : profiles_) {
            const QString name = QString::fromStdString(profile.name);
            if (familyOf(name) != family) continue;
            auto* item = new QTreeWidgetItem(familyItem);
            item->setText(0, name);
            item->setData(0, Qt::UserRole, name);
        }
    }
    tree_->expandAll();
    tree_->blockSignals(treeWasBlocked);
    applyFilter();
}

void ProfilePickerDialog::applyFilter() {
    const QString filter = filterEdit_->text();
    const bool showAll = showAllCheck_->isChecked();
    bool anyVisible = false;
    const bool treeWasBlocked = tree_->signalsBlocked();
    tree_->blockSignals(true);
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* familyItem = tree_->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < familyItem->childCount(); ++j) {
            QTreeWidgetItem* child = familyItem->child(j);
            const QString name = child->data(0, Qt::UserRole).toString();
            const bool visible = showAll || matchesFilter(name, filter);
            child->setHidden(!visible);
            if (visible) ++visibleChildren;
        }
        const bool familyVisible = visibleChildren > 0;
        familyItem->setHidden(!familyVisible);
        if (familyVisible) anyVisible = true;
    }
    // Filtre, su an secili kaydi gizlediyse aile duzeyinde ilk gorunur
    // kayda kaymaz (kullanici secimine mudahale etme); sadece agaci
    // daraltir.
    tree_->blockSignals(treeWasBlocked);
    (void)anyVisible;
}

void ProfilePickerDialog::syncFromTree() {
    QTreeWidgetItem* current = tree_->currentItem();
    if (!current || !current->parent()) return;
    const QString name = current->data(0, Qt::UserRole).toString();
    if (name.isEmpty()) return;
    suppressSync_ = true;
    nameEdit_->setText(name);
    suppressSync_ = false;
    selectProfile(name);
}

void ProfilePickerDialog::syncToTree() {
    const QString typed = nameEdit_->text().trimmed();
    if (typed.isEmpty()) {
        selectedProfile_.clear();
        populateProperties(nullptr);
        sectionLabel_->clear();
        return;
    }
    // Buyuk/kucuk harf duyarsiz canonical eslesme.
    QString canonical;
    for (const auto& profile : profiles_) {
        const QString name = QString::fromStdString(profile.name);
        if (QString::compare(name, typed, Qt::CaseInsensitive) == 0) {
            canonical = name;
            break;
        }
    }
    suppressSync_ = true;
    if (!canonical.isEmpty()) {
        // Alan canonical'a cevrilir (heb240 -> HEB240).
        nameEdit_->setText(canonical);
        // Agacta eslesen kaydi sec (filtre onu gizlemis olabilir; o zaman
        // filtreyi gevset: Tüm profilleri göster).
        bool found = false;
        for (int i = 0; i < tree_->topLevelItemCount() && !found; ++i) {
            QTreeWidgetItem* familyItem = tree_->topLevelItem(i);
            for (int j = 0; j < familyItem->childCount(); ++j) {
                QTreeWidgetItem* child = familyItem->child(j);
                if (child->data(0, Qt::UserRole).toString() == canonical) {
                    tree_->setCurrentItem(child);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // Filtre yüzünden gizli; filtreyi temizle ve tekrar dene.
            filterEdit_->clear();
            applyFilter();
            for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
                QTreeWidgetItem* familyItem = tree_->topLevelItem(i);
                for (int j = 0; j < familyItem->childCount(); ++j) {
                    QTreeWidgetItem* child = familyItem->child(j);
                    if (child->data(0, Qt::UserRole).toString() == canonical) {
                        tree_->setCurrentItem(child);
                        break;
                    }
                }
            }
        }
    }
    selectProfile(canonical);
    suppressSync_ = false;
}

void ProfilePickerDialog::selectProfile(const QString& canonicalName) {
    selectedProfile_ = canonicalName;
    const SteelProfile* profile = currentProfile();
    populateProperties(profile);
    sectionLabel_->setText(canonicalName);
    static_cast<SectionPreviewWidget*>(sectionPreview_)->setProfile(profile);
}

const SteelProfile* ProfilePickerDialog::currentProfile() const {
    for (const auto& profile : profiles_) {
        if (QString::compare(QString::fromStdString(profile.name),
                             selectedProfile_, Qt::CaseInsensitive) == 0)
            return &profile;
    }
    return nullptr;
}

void ProfilePickerDialog::populateProperties(const SteelProfile* profile) {
    struct Row {
        const char* property;
        const char* symbol;
        double value;
        const char* unit;
    };
    // "Katalogda ne varsa": 0 olanlar "-" gorunur (formatValue).
    if (!profile) {
        propsTable_->setRowCount(0);
        return;
    }
    const Row rows[] = {
        {"Profil tipi", "type", static_cast<double>(profile->type), ""},
        {"Alt tip", "subType", static_cast<double>(profile->subType), ""},
        {"Yükseklik", "h", profile->height, "mm"},
        {"Genişlik", "b", profile->width, "mm"},
        {"Gövde kalınlığı", "tw", profile->plateThickness, "mm"},
        {"Flanş kalınlığı", "tf", profile->flangeThickness, "mm"},
        {"Köşe yarıçapı", "r1", profile->roundingRadius, "mm"},
        {"Kesit alanı", "A", profile->crossSectionArea, "mm²"},
        {"Ağırlık", "G", profile->weightPerUnitLength, "kg/m"},
        {"Atalet (y)", "Iy", profile->inertiaY, "mm⁴"},
        {"Atalet (z)", "Iz", profile->inertiaX, "mm⁴"},
        {"Mukavemet modülü (y)", "Wy", profile->sectionModulusY, "mm³"},
        {"Mukavemet modülü (z)", "Wz", profile->sectionModulusX, "mm³"},
        {"Atalet yarıçapı (y)", "iy", profile->radiusOfGyrationY, "mm"},
        {"Atalet yarıçapı (z)", "iz", profile->radiusOfGyrationX, "mm"},
    };
    propsTable_->setRowCount(static_cast<int>(std::size(rows)));
    for (int i = 0; i < static_cast<int>(std::size(rows)); ++i) {
        propsTable_->setItem(i, 0,
                             new QTableWidgetItem(QString::fromUtf8(rows[i].property)));
        propsTable_->setItem(i, 1,
                             new QTableWidgetItem(QString::fromUtf8(rows[i].symbol)));
        propsTable_->setItem(i, 2,
                             new QTableWidgetItem(formatValue(rows[i].value)));
        propsTable_->setItem(i, 3,
                             new QTableWidgetItem(QString::fromUtf8(rows[i].unit)));
    }
    propsTable_->resizeColumnsToContents();
}

void ProfilePickerDialog::setInitialName(const QString& name) {
    if (name.isEmpty()) return;
    suppressSync_ = true;
    nameEdit_->setText(name);
    suppressSync_ = false;
    syncToTree();
}

void ProfilePickerDialog::focusFilter() {
    filterEdit_->setFocus();
    filterEdit_->selectAll();
}

QString ProfilePickerDialog::profileName() const { return nameEdit_->text(); }

void ProfilePickerDialog::onApplyClicked() {
    // Alan yazilmis ama agac senkronu yapilmamis olabilir (odak kaybi).
    syncToTree();
    emit profileApplied(selectedProfile_);
}

void ProfilePickerDialog::onOkClicked() {
    syncToTree();
    emit profileApplied(selectedProfile_);
    accept();
}

// ---------------------------------------------------------------------------
// MaterialPickerDialog
// ---------------------------------------------------------------------------
MaterialPickerDialog::MaterialPickerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Malzeme Seçimi"));
    resize(420, 480);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);
    topRow->addWidget(new QLabel(QStringLiteral("Filtre:"), this));
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setObjectName(QStringLiteral("materialFilter"));
    filterEdit_->setPlaceholderText(QStringLiteral("S3* ..."));
    topRow->addWidget(filterEdit_, 1);
    outer->addLayout(topRow);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("materialList"));
    outer->addWidget(list_, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    okButton_ = new QPushButton(QStringLiteral("Tamam"), this);
    okButton_->setObjectName(QStringLiteral("materialOkButton"));
    okButton_->setDefault(true);
    applyButton_ = new QPushButton(QStringLiteral("Uygula"), this);
    applyButton_->setObjectName(QStringLiteral("materialApplyButton"));
    auto* cancelButton = new QPushButton(QStringLiteral("İptal"), this);
    cancelButton->setObjectName(QStringLiteral("materialCancelButton"));
    bottom->addWidget(okButton_);
    bottom->addWidget(applyButton_);
    bottom->addWidget(cancelButton);
    outer->addLayout(bottom);

    connect(filterEdit_, &QLineEdit::textChanged, this,
            &MaterialPickerDialog::applyFilter);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onOkClicked(); });
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { onOkClicked(); });
    connect(okButton_, &QPushButton::clicked, this,
            &MaterialPickerDialog::onOkClicked);
    connect(applyButton_, &QPushButton::clicked, this,
            &MaterialPickerDialog::onApplyClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void MaterialPickerDialog::setMaterials(const QStringList& items) {
    materials_ = items;
    list_->clear();
    for (const QString& item : materials_) {
        auto* entry = new QListWidgetItem(item);
        entry->setData(Qt::UserRole, item);
        list_->addItem(entry);
    }
}

void MaterialPickerDialog::setInitialName(const QString& name) {
    if (name.isEmpty()) return;
    for (int i = 0; i < list_->count(); ++i) {
        if (QString::compare(list_->item(i)->data(Qt::UserRole).toString(), name,
                             Qt::CaseInsensitive) == 0) {
            list_->setCurrentRow(i);
            selectedMaterial_ = list_->item(i)->data(Qt::UserRole).toString();
            return;
        }
    }
}

void MaterialPickerDialog::focusFilter() {
    filterEdit_->setFocus();
    filterEdit_->selectAll();
}

void MaterialPickerDialog::applyFilter() {
    const QString filter = filterEdit_->text();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        item->setHidden(!matchesFilter(item->data(Qt::UserRole).toString(),
                                       filter));
    }
}

void MaterialPickerDialog::onApplyClicked() {
    QListWidgetItem* current = list_->currentItem();
    if (current)
        selectedMaterial_ = current->data(Qt::UserRole).toString();
    emit materialApplied(selectedMaterial_);
}

void MaterialPickerDialog::onOkClicked() {
    QListWidgetItem* current = list_->currentItem();
    if (current)
        selectedMaterial_ = current->data(Qt::UserRole).toString();
    emit materialApplied(selectedMaterial_);
    accept();
}

} // namespace mm
