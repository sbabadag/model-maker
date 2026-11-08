#include "model_maker/properties_panel.hpp"

#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace mm {

namespace {
const QString kCaptionStyle = QStringLiteral("color: #C8CCD4; font-size: 8pt;");
const QString kValueStyle = QStringLiteral("color: #F0F0F4; font-size: 9pt;");

QWidget* makeHeader(const QString& text, QWidget* parent, const QString& objectName) {
    QToolButton* header = new QToolButton(parent);
    header->setObjectName(objectName);
    header->setText(text);
    header->setCheckable(true);
    header->setChecked(true);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setArrowType(Qt::DownArrow);
    header->setAutoRaise(true);
    header->setStyleSheet(
        QStringLiteral("QToolButton { color: #D8DCE4; font-weight: 700; "
                       "border: none; padding: 2px 0; }"));
    return header;
}
} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(4);

    title_ = new QLabel(QStringLiteral("— (0 seçili)"), this);
    title_->setObjectName(QStringLiteral("propsTitle"));
    title_->setStyleSheet(
        QStringLiteral("color: #F0F0F4; font-size: 10pt; font-weight: 700;"));
    outer->addWidget(title_);

    info_ = new QLabel(this);
    info_->setObjectName(QStringLiteral("propsWeldAsm"));
    info_->setWordWrap(true);
    info_->setStyleSheet(QStringLiteral("color: #D0B37E; font-size: 8.5pt;"));
    info_->setVisible(false);
    outer->addWidget(info_);

    // ---- GENEL ----
    QWidget* genel = addSection(QStringLiteral("Genel"),
                                QStringLiteral("sectionGenel"));
    nameEdit_ = new QLineEdit(genel);
    nameEdit_->setObjectName(QStringLiteral("propsName"));
    fieldRow(genel, QStringLiteral("Ad"), nameEdit_);

    QWidget* profileRow = new QWidget(genel);
    QHBoxLayout* profileRowLayout = new QHBoxLayout(profileRow);
    profileRowLayout->setContentsMargins(0, 0, 0, 0);
    profileRowLayout->setSpacing(4);
    profileEdit_ = new QLineEdit(profileRow);
    profileEdit_->setObjectName(QStringLiteral("propsProfile"));
    profileEdit_->setPlaceholderText(QStringLiteral("HEB240..."));
    profileEdit_->setToolTip(
        QStringLiteral("Profil adı elle yazılır (büyük/küçük harf duyarsız, "
                       "otomatik tamamlama).\nKatalogda yoksa alan kırmızı olur "
                       "ve uygulanmaz.\nX tuşu bu alana odaklanır."));
    profileCompleter_ = new QCompleter(this);
    profileCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    profileCompleter_->setFilterMode(Qt::MatchContains);
    profileEdit_->setCompleter(profileCompleter_);
    profileRowLayout->addWidget(profileEdit_, 1);
    profileButton_ = new QToolButton(profileRow);
    profileButton_->setObjectName(QStringLiteral("propsProfileButton"));
    profileButton_->setText(QStringLiteral("…"));
    profileButton_->setToolTip(QStringLiteral(
        "Profil seçim penceresi (UI-4): aile ağacı, filtre, kesit resmi."));
    profileButton_->setEnabled(true);
    connect(profileButton_, &QToolButton::clicked, this, [this]() {
        if (!onProfileBrowse) return;
        applyProfileFromPicker(onProfileBrowse());
    });
    profileRowLayout->addWidget(profileButton_);
    fieldRow(genel, QStringLiteral("Profil"), profileRow);

    QWidget* materialRow = new QWidget(genel);
    QHBoxLayout* materialRowLayout = new QHBoxLayout(materialRow);
    materialRowLayout->setContentsMargins(0, 0, 0, 0);
    materialRowLayout->setSpacing(4);
    materialEdit_ = new QLineEdit(materialRow);
    materialEdit_->setObjectName(QStringLiteral("propsMaterial"));
    materialEdit_->setPlaceholderText(QStringLiteral("S355J2..."));
    materialEdit_->setToolTip(QStringLiteral(
        "Malzeme adı elle yazılır (büyük/küçük harf duyarsız).\nListede "
        "yoksa alan kırmızı olur ve uygulanmaz."));
    materialCompleter_ = new QCompleter(this);
    materialCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    materialCompleter_->setFilterMode(Qt::MatchContains);
    materialEdit_->setCompleter(materialCompleter_);
    materialRowLayout->addWidget(materialEdit_, 1);
    materialButton_ = new QToolButton(materialRow);
    materialButton_->setObjectName(QStringLiteral("propsMaterialButton"));
    materialButton_->setText(QStringLiteral("…"));
    materialButton_->setToolTip(QStringLiteral(
        "Malzeme seçim penceresi (UI-4): filtre + liste."));
    materialButton_->setEnabled(true);
    connect(materialButton_, &QToolButton::clicked, this, [this]() {
        if (!onMaterialBrowse) return;
        applyMaterialFromPicker(onMaterialBrowse());
    });
    materialRowLayout->addWidget(materialButton_);
    fieldRow(genel, QStringLiteral("Malzeme"), materialRow);

    classCombo_ = new QComboBox(genel);
    classCombo_->setObjectName(QStringLiteral("propsClass"));
    fieldRow(genel, QStringLiteral("Sınıf"), classCombo_);

    finishCombo_ = new QComboBox(genel);
    finishCombo_->setObjectName(QStringLiteral("propsFinish"));
    finishCombo_->setEditable(true);
    finishCombo_->setInsertPolicy(QComboBox::NoInsert);
    fieldRow(genel, QStringLiteral("Yüzey işlemi"), finishCombo_);

    // ---- KONUM (modeldeki mevcut alanlar) ----
    QWidget* konum = addSection(QStringLiteral("Konum"),
                                QStringLiteral("sectionKonum"));
    rotationSpin_ = new QDoubleSpinBox(konum);
    rotationSpin_->setObjectName(QStringLiteral("propsRotation"));
    rotationSpin_->setRange(0.0, 360.0);
    rotationSpin_->setDecimals(1);
    rotationSpin_->setSingleStep(5.0);
    rotationSpin_->setSuffix(QStringLiteral("°"));
    fieldRow(konum, QStringLiteral("Dönüş"), rotationSpin_);

    // ---- GORUNUM (mevcut islevsellik korunur) ----
    QWidget* gorsunum = addSection(QStringLiteral("Görünüm"),
                                   QStringLiteral("sectionGorunum"));
    layerCombo_ = new QComboBox(gorsunum);
    layerCombo_->setObjectName(QStringLiteral("propsLayer"));
    fieldRow(gorsunum, QStringLiteral("Katman"), layerCombo_);

    lineTypeCombo_ = new QComboBox(gorsunum);
    lineTypeCombo_->setObjectName(QStringLiteral("propsLineType"));
    fieldRow(gorsunum, QStringLiteral("Çizgi Tipi"), lineTypeCombo_);

    // ---- KOLON ----
    QWidget* kolon = addSection(QStringLiteral("Kolon"),
                                QStringLiteral("sectionKolon"));
    columnTopSpin_ = new QDoubleSpinBox(kolon);
    columnTopSpin_->setObjectName(QStringLiteral("propsColumnTop"));
    columnTopSpin_->setRange(-100000.0, 100000.0);
    columnTopSpin_->setDecimals(0);
    columnTopSpin_->setValue(8500.0);
    fieldRow(kolon, QStringLiteral("Kolon Üst (mm)"), columnTopSpin_);

    columnBottomSpin_ = new QDoubleSpinBox(kolon);
    columnBottomSpin_->setObjectName(QStringLiteral("propsColumnBottom"));
    columnBottomSpin_->setRange(-100000.0, 100000.0);
    columnBottomSpin_->setDecimals(0);
    columnBottomSpin_->setValue(0.0);
    fieldRow(kolon, QStringLiteral("Kolon Alt (mm)"), columnBottomSpin_);

    lengthLabel_ = new QLabel(QStringLiteral("-"), kolon);
    lengthLabel_->setObjectName(QStringLiteral("propsLength"));
    lengthLabel_->setStyleSheet(kValueStyle);
    fieldRow(kolon, QStringLiteral("Uzunluk"), lengthLabel_);

    // ---- ALT CUBUK ----
    applyButton_ = new QPushButton(QStringLiteral("UYGULA"), this);
    applyButton_->setObjectName(QStringLiteral("propsApply"));
    applyButton_->setStyleSheet(
        QStringLiteral("QPushButton { background: #2E6B4F; color: #F0F0F4; "
                       "font-weight: 700; padding: 6px; border-radius: 3px; }"
                       "QPushButton:hover { background: #37805F; }"
                       "QPushButton:disabled { background: #3A4150; "
                       "color: #7A8090; }"));
    applyButton_->setEnabled(false);
    outer->addWidget(applyButton_);

    columnPlaceButton_ = new QPushButton(QStringLiteral("KOLON YERLEŞTİR"), this);
    columnPlaceButton_->setObjectName(QStringLiteral("propsColumnBtn"));
    columnPlaceButton_->setStyleSheet(
        QStringLiteral("QPushButton { background: #3A5A8C; color: #F0F0F4; "
                       "font-weight: 700; padding: 6px; border-radius: 3px; }"
                       "QPushButton:hover { background: #486FAB; }"));
    outer->addWidget(columnPlaceButton_);
    outer->addStretch(1);

    // ---- BAGLANTILAR (kirli bayraklari yalniz kullanici degisikliginde) ----
    QObject::connect(nameEdit_, &QLineEdit::textEdited, this, [this]() {
        nameDirty_ = true;
        updateApplyEnabled();
    });
    QObject::connect(profileEdit_, &QLineEdit::textEdited, this, [this]() {
        markProfileDirty();
        if (validateProfile) setProfileValid(validateProfile(profileText()));
    });
    QObject::connect(profileEdit_, &QLineEdit::returnPressed, this, [this]() {
        if (validateProfile && !validateProfile(profileText())) {
            setProfileValid(false);
            return;  // kirli kalir; UYGULA da reddedecek
        }
        if (onProfileEnter) onProfileEnter(profileText());
        profileDirty_ = false;
        updateApplyEnabled();
    });
    QObject::connect(profileCompleter_,
        QOverload<const QString&>::of(&QCompleter::activated), this,
        [this](const QString& text) {
            profileEdit_->setText(text);
            if (onProfileEnter) onProfileEnter(text);
            profileDirty_ = false;
            updateApplyEnabled();
        });
    QObject::connect(materialEdit_, &QLineEdit::textEdited, this, [this]() {
        markMaterialDirty();
        if (validateMaterial) setMaterialValid(validateMaterial(materialText()));
    });
    QObject::connect(materialEdit_, &QLineEdit::returnPressed, this, [this]() {
        if (validateMaterial && !validateMaterial(materialText())) {
            setMaterialValid(false);
            return;  // kirli kalir; UYGULA da reddedecek
        }
        if (onMaterialEnter) onMaterialEnter(materialText());
        materialDirty_ = false;
        updateApplyEnabled();
    });
    QObject::connect(materialCompleter_,
        QOverload<const QString&>::of(&QCompleter::activated), this,
        [this](const QString& text) {
            materialEdit_->setText(text);
            if (onMaterialEnter) onMaterialEnter(text);
            materialDirty_ = false;
            updateApplyEnabled();
        });
    QObject::connect(classCombo_, QOverload<int>::of(&QComboBox::activated), this,
        [this](int index) {
            if (onClassPicked) onClassPicked(index);
            classDirty_ = false;
            updateApplyEnabled();
        });
    QObject::connect(finishCombo_, &QComboBox::currentTextChanged, this,
        [this](const QString&) {
            finishDirty_ = true;
            updateApplyEnabled();
        });
    QObject::connect(layerCombo_, QOverload<int>::of(&QComboBox::activated), this,
        [this](int) {
            if (onLayerPicked) onLayerPicked(layerCombo_->currentText());
        });
    QObject::connect(lineTypeCombo_, QOverload<int>::of(&QComboBox::activated),
        this, [this](int index) {
            if (onLineTypePicked) onLineTypePicked(index);
        });
    QObject::connect(rotationSpin_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) {
            rotationDirty_ = true;
            updateApplyEnabled();
        });
    QObject::connect(columnTopSpin_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) {
            columnDirty_ = true;
            updateApplyEnabled();
            if (onColumnLevelsEdit)
                onColumnLevelsEdit(columnTopSpin_->value(),
                                   columnBottomSpin_->value());
        });
    QObject::connect(columnBottomSpin_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) {
            columnDirty_ = true;
            updateApplyEnabled();
            if (onColumnLevelsEdit)
                onColumnLevelsEdit(columnTopSpin_->value(),
                                   columnBottomSpin_->value());
        });
    const auto columnEnter = [this]() {
        if (!columnDirty_) return;
        if (onColumnLevelsEnter) onColumnLevelsEnter();
        columnDirty_ = false;
        updateApplyEnabled();
    };
    QObject::connect(columnTopSpin_, &QDoubleSpinBox::editingFinished, this,
                     columnEnter);
    QObject::connect(columnBottomSpin_, &QDoubleSpinBox::editingFinished, this,
                     columnEnter);
    QObject::connect(applyButton_, &QPushButton::clicked, this, [this]() {
        if (onApplyClicked) onApplyClicked();
    });
    QObject::connect(columnPlaceButton_, &QPushButton::clicked, this, [this]() {
        if (onColumnPlaceClicked) onColumnPlaceClicked();
    });
}

QWidget* PropertiesPanel::addSection(const QString& title,
                                     const QString& objectName) {
    QWidget* header = makeHeader(title, this, objectName);
    layout()->addWidget(header);
    QWidget* content = new QWidget(this);
    content->setObjectName(objectName + QStringLiteral("Body"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(6, 2, 0, 6);
    contentLayout->setSpacing(4);
    layout()->addWidget(content);
    const auto toggle = [content, headerWidget = static_cast<QToolButton*>(header)](
                            bool checked) {
        content->setVisible(checked);
        headerWidget->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    };
    QObject::connect(static_cast<QToolButton*>(header), &QToolButton::toggled,
                     this, toggle);
    return content;
}

QWidget* PropertiesPanel::fieldRow(QWidget* parent, const QString& caption,
                                   QWidget* control) {
    QWidget* row = new QWidget(parent);
    QVBoxLayout* rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(1);
    QLabel* captionLabel = new QLabel(caption, row);
    captionLabel->setStyleSheet(kCaptionStyle);
    rowLayout->addWidget(captionLabel);
    rowLayout->addWidget(control);
    parent->layout()->addWidget(row);
    return row;
}

void PropertiesPanel::setTitle(const QString& text) {
    title_->setText(text);
}

void PropertiesPanel::setInfo(const QString& text) {
    info_->setText(text);
    info_->setVisible(!text.isEmpty());
}

void PropertiesPanel::setLengthLabel(const QString& text) {
    lengthLabel_->setText(text);
}

void PropertiesPanel::setProfileCatalog(const QStringList& names) {
    auto* model = new QStringListModel(names, this);
    profileCompleter_->setModel(model);
}

void PropertiesPanel::setProfile(const QString& text) {
    QSignalBlocker blocker(profileEdit_);
    profileEdit_->setText(text);
    if (validateProfile) setProfileValid(validateProfile(text));
}

void PropertiesPanel::setProfileValid(bool valid) {
    profileValid_ = valid;
    profileEdit_->setStyleSheet(valid
        ? QString()
        : QStringLiteral("QLineEdit { border: 1px solid #C0392B; }"));
}

void PropertiesPanel::setMaterialChoices(const QStringList& items) {
    auto* model = new QStringListModel(items, this);
    materialCompleter_->setModel(model);
}

void PropertiesPanel::setMaterial(const QString& text) {
    QSignalBlocker blocker(materialEdit_);
    materialEdit_->setText(text);
    if (validateMaterial) setMaterialValid(validateMaterial(text));
}

void PropertiesPanel::setMaterialValid(bool valid) {
    materialEdit_->setStyleSheet(valid
        ? QString()
        : QStringLiteral("QLineEdit { border: 1px solid #C0392B; }"));
}

void PropertiesPanel::setName(const QString& text) {
    QSignalBlocker blocker(nameEdit_);
    nameEdit_->setText(text);
}

void PropertiesPanel::setFinishChoices(const QStringList& items) {
    QSignalBlocker blocker(finishCombo_);
    finishCombo_->clear();
    finishCombo_->addItems(items);
}

void PropertiesPanel::setFinish(const QString& text) {
    QSignalBlocker blocker(finishCombo_);
    finishCombo_->setCurrentText(text);
}

void PropertiesPanel::setClassChoices(const QStringList& labels,
                                      const QList<QColor>& colors) {
    QSignalBlocker blocker(classCombo_);
    classCombo_->clear();
    for (int i = 0; i < labels.size(); ++i) {
        QPixmap swatch(14, 14);
        swatch.fill(colors.value(i, QColor(QStringLiteral("#808080"))));
        classCombo_->addItem(QIcon(swatch), labels.at(i));
    }
}

void PropertiesPanel::setClassIndex(int index) {
    QSignalBlocker blocker(classCombo_);
    classCombo_->setCurrentIndex(index);
}

void PropertiesPanel::setLayerChoices(const QStringList& items) {
    QSignalBlocker blocker(layerCombo_);
    layerCombo_->clear();
    layerCombo_->addItems(items);
}

void PropertiesPanel::setLayer(const QString& text) {
    QSignalBlocker blocker(layerCombo_);
    layerCombo_->setCurrentText(text);
}

void PropertiesPanel::setLineTypeChoices(const QStringList& items) {
    QSignalBlocker blocker(lineTypeCombo_);
    lineTypeCombo_->clear();
    lineTypeCombo_->addItems(items);
}

void PropertiesPanel::setLineType(const QString& text) {
    QSignalBlocker blocker(lineTypeCombo_);
    if (!text.isEmpty()) lineTypeCombo_->setCurrentText(text);
}

void PropertiesPanel::setRotation(double degrees) {
    QSignalBlocker blocker(rotationSpin_);
    rotationSpin_->setValue(degrees);
}

void PropertiesPanel::setColumnLevels(double top, double bottom) {
    QSignalBlocker blockerTop(columnTopSpin_);
    QSignalBlocker blockerBottom(columnBottomSpin_);
    columnTopSpin_->setValue(top);
    columnBottomSpin_->setValue(bottom);
}

void PropertiesPanel::setApplyEnabled(bool enabled) {
    applyButton_->setEnabled(enabled);
}

void PropertiesPanel::focusProfileEdit() {
    if (!profileEdit_->isEnabled()) return;
    profileEdit_->setFocus(Qt::ShortcutFocusReason);
    profileCompleter_->complete();
}

QString PropertiesPanel::profileText() const {
    return profileEdit_->text().trimmed();
}

void PropertiesPanel::applyProfileFromPicker(const QString& canonicalName) {
    if (canonicalName.isEmpty()) return;
    profileEdit_->setText(canonicalName);
    markProfileDirty();
    setProfileValid(!validateProfile || validateProfile(canonicalName));
}

void PropertiesPanel::applyMaterialFromPicker(const QString& canonicalName) {
    if (canonicalName.isEmpty()) return;
    materialEdit_->setText(canonicalName);
    markMaterialDirty();
    setMaterialValid(!validateMaterial || validateMaterial(canonicalName));
}

QString PropertiesPanel::materialText() const {
    return materialEdit_->text().trimmed();
}

QString PropertiesPanel::name() const {
    return nameEdit_->text().trimmed();
}

QString PropertiesPanel::finish() const {
    return finishCombo_->currentText().trimmed();
}

int PropertiesPanel::classIndex() const {
    return classCombo_->currentIndex();
}

double PropertiesPanel::rotation() const {
    return rotationSpin_->value();
}

double PropertiesPanel::columnTop() const {
    return columnTopSpin_->value();
}

double PropertiesPanel::columnBottom() const {
    return columnBottomSpin_->value();
}

bool PropertiesPanel::isAnyDirty() const noexcept {
    return profileDirty_ || materialDirty_ || nameDirty_ || finishDirty_ ||
           classDirty_ || rotationDirty_ || columnDirty_;
}

void PropertiesPanel::clearDirtyFlags() {
    profileDirty_ = materialDirty_ = nameDirty_ = finishDirty_ = false;
    classDirty_ = rotationDirty_ = columnDirty_ = false;
    updateApplyEnabled();
}

void PropertiesPanel::markProfileDirty() {
    profileDirty_ = true;
    updateApplyEnabled();
}

void PropertiesPanel::markMaterialDirty() {
    materialDirty_ = true;
    updateApplyEnabled();
}

void PropertiesPanel::updateApplyEnabled() {
    applyButton_->setEnabled(isAnyDirty());
}

} // namespace mm
