#include "model_maker/qt_main_window.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/two_point_view.hpp"
#include "model_maker/component_dialog_widget.hpp"
#include "model_maker/component_catalog.hpp"

#include <QApplication>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QCloseEvent>
#include <QMenu>
#include <QToolButton>
#include <QLabel>
#include <QStatusBar>
#include <QStandardItemModel>
#include <cctype>
#include <map>
#include <memory>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QFrame>
#include <QPushButton>
#include <QComboBox>
#include <QCompleter>
#include <QVariant>
#include <QColorDialog>
#include <QCheckBox>
#include <QStringList>
#include <optional>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMenu>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFont>
#include <functional>
#include <variant>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QTimer>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QLabel>
#include <QWindow>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEvent>
#include <QPainter>
#include <QPolygon>
#include <QPixmap>
#include <QIcon>
#include <QPen>
#include <QBrush>
#include <cmath>
#include <QCoreApplication>
#include <filesystem>
#include "model_maker/component_presets.hpp"
#include "model_maker/drawing_sheet_widget.hpp"
#include "model_maker/progress_overlay.hpp"

namespace {
// Component ayar setleri (Tekla Save / Save as / Load): exe'nin yanindaki
// component_presets.json. Ilk kullanimda yuklenir; bozuk dosya SESSIZCE ezilmez —
// once .bozuk yedegi alinir, sonra bos depo ile devam edilir.
std::filesystem::path componentPresetPath() {
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
           L"component_presets.json";
}
mm::ComponentPresetStore& componentPresetStore() {
    static mm::ComponentPresetStore store;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string err;
        if (!store.load(componentPresetPath(), err)) {
            std::error_code ec;
            std::filesystem::path backup = componentPresetPath();
            backup += L".bozuk";
            std::filesystem::copy_file(componentPresetPath(), backup,
                                       std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
    return store;
}
bool persistComponentPresets(std::string& err) {
    return componentPresetStore().save(componentPresetPath(), err);
}
// Menuden yeni yerlestirilen bilesen: kayitli "standard" set varsa onunla baslar,
// yoksa bos (motor/sema varsayilanlari).
std::vector<mm::CompParam> standardComponentParams(const std::wstring& typeId) {
    const mm::ComponentPreset* p = componentPresetStore().find(typeId, mm::kStandardPresetName);
    return p ? p->params : std::vector<mm::CompParam>{};
}
} // namespace

namespace {
// AutoCAD tarzi renkli ribbon ikonlari: QPainter ile runtime'da cizilir
// (harici asset yok). 24x24, antialiased, koyu tema uzerinde okunur renkler.
enum class ToolGlyph {
    Line, Polyline, Rect, Circle, Face3D,
    Undo, Redo, Pan, Move, Copy, Offset, Mirror, Delete,
    LinearArray, PolarArray, Trim, Extend, Fillet,
    Toggle3D, Extents, Plane, Reset, New, Open, Save, Import, Export, Bolt, Plate
};

static QIcon makeToolIcon(ToolGlyph glyph) {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen;
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    const QColor blue(74, 144, 217), yellow(245, 194, 75), green(87, 193, 90),
        red(226, 92, 92), cyan(85, 201, 216), orange(232, 163, 61),
        gray(154, 160, 166), purple(180, 140, 232), lightblue(127, 179, 232),
        white(232, 232, 232);
    switch (glyph) {
    case ToolGlyph::Line:
        pen.setColor(blue); p.setPen(pen); p.drawLine(4, 20, 20, 4); break;
    case ToolGlyph::Polyline:
        pen.setColor(yellow); p.setPen(pen);
        p.drawPolyline(QPolygonF() << QPointF(4, 20) << QPointF(8, 11)
                                   << QPointF(13, 15) << QPointF(20, 4)); break;
    case ToolGlyph::Rect:
        pen.setColor(green); p.setPen(pen); p.drawRect(4, 6, 16, 12); break;
    case ToolGlyph::Circle:
        pen.setColor(red); p.setPen(pen); p.drawEllipse(4, 4, 16, 16); break;
    case ToolGlyph::Face3D:
        pen.setColor(cyan); p.setPen(pen);
        p.drawPolygon(QPolygonF() << QPointF(12, 3) << QPointF(21, 19) << QPointF(3, 19)); break;
    case ToolGlyph::Undo:
        pen.setColor(orange); p.setPen(pen);
        p.drawArc(QRectF(5, 5, 10, 13), 30 * 16, 190 * 16);
        p.drawPolyline(QPolygonF() << QPointF(17, 6) << QPointF(14, 4) << QPointF(15, 8)); break;
    case ToolGlyph::Redo: {
        QIcon base = makeToolIcon(ToolGlyph::Undo);
        QPixmap mirrored(24, 24); mirrored.fill(Qt::transparent);
        QPainter m(&mirrored);
        m.setRenderHint(QPainter::Antialiasing);
        m.translate(24, 0); m.scale(-1, 1);
        base.paint(&m, QRect(0, 0, 24, 24));
        m.end();
        return QIcon(mirrored);
    }
    case ToolGlyph::Pan:
        pen.setColor(gray); p.setPen(pen);
        p.drawLine(5, 5, 19, 19); p.drawLine(19, 5, 5, 19); break;
    case ToolGlyph::Move:
        pen.setColor(purple); p.setPen(pen);
        p.drawLine(12, 2, 12, 22); p.drawLine(2, 12, 22, 12);
        p.setBrush(purple);
        p.drawPolygon(QPolygonF() << QPointF(12, 2) << QPointF(9, 7) << QPointF(15, 7));
        p.drawPolygon(QPolygonF() << QPointF(12, 22) << QPointF(9, 17) << QPointF(15, 17));
        p.drawPolygon(QPolygonF() << QPointF(2, 12) << QPointF(7, 9) << QPointF(7, 15));
        p.drawPolygon(QPolygonF() << QPointF(22, 12) << QPointF(17, 9) << QPointF(17, 15));
        p.setBrush(Qt::NoBrush); break;
    case ToolGlyph::Copy:
        pen.setColor(lightblue); p.setPen(pen); p.drawRect(9, 9, 11, 11);
        pen.setColor(blue); p.setPen(pen); p.drawRect(4, 4, 11, 11); break;
    case ToolGlyph::Offset:
        pen.setColor(green); p.setPen(pen);
        p.drawLine(3, 9, 21, 9); p.drawLine(3, 15, 21, 15);
        p.drawPolyline(QPolygonF() << QPointF(12, 17) << QPointF(12, 7) << QPointF(14, 9)); break;
    case ToolGlyph::Mirror:
        pen.setColor(cyan); p.setPen(pen);
        p.drawPolygon(QPolygonF() << QPointF(4, 8) << QPointF(9, 6) << QPointF(9, 10));
        p.drawPolygon(QPolygonF() << QPointF(20, 8) << QPointF(15, 6) << QPointF(15, 10));
        pen.setStyle(Qt::DashLine); p.drawLine(12, 3, 12, 21); break;
    case ToolGlyph::Delete:
        pen.setColor(red); pen.setWidth(3); p.setPen(pen);
        p.drawLine(5, 5, 19, 19); p.drawLine(19, 5, 5, 19); break;
    case ToolGlyph::LinearArray:
        pen.setColor(white); p.setPen(pen); p.setBrush(white);
        for (const QPointF& pt : {QPointF(5, 6), QPointF(12, 6), QPointF(19, 6),
                                  QPointF(5, 14), QPointF(12, 14), QPointF(19, 14)})
            p.drawEllipse(pt, 2.0, 2.0);
        p.setBrush(Qt::NoBrush); break;
    case ToolGlyph::PolarArray:
        pen.setColor(white); p.setPen(pen); p.setBrush(white);
        for (int i = 0; i < 6; ++i) {
            const double ang = 3.14159265358979323846 / 3.0 * i;
            p.drawEllipse(QPointF(12 + 7 * std::cos(ang), 12 + 7 * std::sin(ang)), 1.8, 1.8);
        }
        p.setBrush(Qt::NoBrush); break;
    case ToolGlyph::Trim:
        pen.setColor(yellow); p.setPen(pen);
        p.drawLine(4, 16, 20, 8);
        p.drawLine(9, 9, 9, 15); p.drawLine(15, 15, 15, 9); break;
    case ToolGlyph::Extend:
        pen.setColor(yellow); p.setPen(pen);
        p.drawLine(6, 18, 16, 8);
        p.drawPolyline(QPolygonF() << QPointF(16, 8) << QPointF(20, 4)
                                   << QPointF(21, 6) << QPointF(18, 9)); break;
    case ToolGlyph::Fillet:
        pen.setColor(orange); p.setPen(pen);
        p.drawLine(4, 19, 4, 9); p.drawLine(4, 9, 19, 9);
        p.drawArc(QRectF(4, 5, 8, 8), 180 * 16, 90 * 16); break;
    case ToolGlyph::Toggle3D:
        pen.setColor(cyan); p.setPen(pen);
        p.drawRect(8, 8, 10, 10);
        p.drawPolygon(QPolygonF() << QPointF(8, 8) << QPointF(13, 3) << QPointF(23, 3) << QPointF(18, 8));
        p.drawPolygon(QPolygonF() << QPointF(18, 8) << QPointF(23, 3) << QPointF(23, 13) << QPointF(18, 18)); break;
    case ToolGlyph::Extents:
        pen.setColor(white); p.setPen(pen);
        p.drawEllipse(5, 5, 12, 12);
        p.drawLine(15, 15, 20, 20); break;
    case ToolGlyph::Plane:
        pen.setColor(cyan); p.setPen(pen);
        p.drawPolygon(QPolygonF() << QPointF(12, 3) << QPointF(21, 12) << QPointF(12, 21) << QPointF(3, 12));
        p.drawLine(12, 3, 12, 21); p.drawLine(3, 12, 21, 12); break;
    case ToolGlyph::Bolt: {
        pen.setColor(yellow); p.setPen(pen);
        QPolygonF hex;
        for (int i = 0; i < 6; ++i) {
            const double a = 3.14159265358979323846 / 3.0 * i;
            hex << QPointF(12 + 6 * std::cos(a), 7 + 5 * std::sin(a));
        }
        p.drawPolygon(hex);
        p.drawLine(12, 12, 12, 21);
        p.drawLine(9, 18, 15, 18);
        break;
    }
    case ToolGlyph::Plate: {
        // Alin levhasi: dik levha + yatay kiris + bulon noktalari.
        pen.setColor(green); p.setPen(pen);
        p.drawRect(4, 5, 6, 14);         // levha (kolon flansi tarafi)
        pen.setColor(cyan); p.setPen(pen);
        p.drawLine(10, 12, 21, 12);      // kiris
        pen.setColor(yellow); p.setPen(pen);
        p.drawEllipse(QPointF(12, 9), 1, 1); p.drawEllipse(QPointF(12, 15), 1, 1); break;
    }
    case ToolGlyph::Reset:
        pen.setColor(orange); p.setPen(pen);
        p.drawArc(QRectF(5, 5, 14, 14), 40 * 16, 280 * 16);
        p.drawPolygon(QPolygonF() << QPointF(19, 4) << QPointF(15, 2) << QPointF(17, 7)); break;
    case ToolGlyph::New:
        pen.setColor(white); p.setPen(pen);
        p.drawRect(6, 4, 12, 16); p.drawLine(6, 9, 18, 9);
        pen.setColor(green); p.drawLine(10, 12, 14, 12); p.drawLine(12, 10, 12, 14); break;
    case ToolGlyph::Open:
        pen.setColor(orange); p.setPen(pen);
        p.drawPolygon(QPolygonF() << QPointF(3, 8) << QPointF(9, 8) << QPointF(11, 10)
                                  << QPointF(21, 10) << QPointF(21, 19) << QPointF(3, 19)); break;
    case ToolGlyph::Save:
        pen.setColor(blue); p.setPen(pen);
        p.drawRect(6, 3, 12, 18); p.drawRect(8, 3, 8, 5); p.drawLine(8, 15, 16, 15); break;
    case ToolGlyph::Import:
        pen.setColor(green); p.setPen(pen);
        p.drawPolyline(QPolygonF() << QPointF(3, 17) << QPointF(14, 17) << QPointF(14, 7)
                                   << QPointF(21, 7));
        p.drawPolyline(QPolygonF() << QPointF(17, 4) << QPointF(21, 7) << QPointF(17, 10)); break;
    case ToolGlyph::Export:
        pen.setColor(blue); p.setPen(pen);
        p.drawPolyline(QPolygonF() << QPointF(3, 7) << QPointF(10, 7) << QPointF(10, 17)
                                   << QPointF(21, 17));
        p.drawPolyline(QPolygonF() << QPointF(17, 14) << QPointF(21, 17) << QPointF(17, 20)); break;
    }
    p.end();
    return QIcon(pm);
}
} // namespace
#include <algorithm>
#include <fstream>
#include <string>

#include <windows.h>

#include "model_maker/build_info.hpp"

namespace mm {

namespace {

// Props panelindeki profil combo'sunun "profilsiz" yer tutucusu.
QString profileNoneLabel() { return QString::fromUtf8("— Profilsiz (düz çizgi) —"); }

// Combo'dan secili profil adini okur. Yer tutucu (UserRole bos) ise ve kullanici
// elle bir ad yazmadiysa bos string doner -> profilsiz cizim.
std::string comboProfileName(const QComboBox* combo) {
    if (!combo) return {};
    const QVariant data = combo->currentData();
    if (data.isValid()) return data.toString().trimmed().toStdString();
    return combo->currentText().trimmed().toStdString(); // elle yazilan ad
}

// DOGRUSAL KOPYA/TASIMA DELTA PANELI (kullanici talebi): X / Y / Z alanlari +
// "Iki Nokta Sec" dugmesi. Kullanici degerleri yazar ve Tamam der; kopyalama/
// tasima tam o kadar oteye yapilir. "Iki Nokta Sec" secilirse panel kapanir ve
// delta ekranda iki tiklamayla (2. nokta - 1. nokta) belirlenir.
bool promptLinearDeltaDialog(QWidget* parent, Vec3& delta, bool& pickTwoPoints) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Doğrusal Kopyala / Taşı — Delta"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* info = new QLabel(
        QStringLiteral("X / Y / Z offsetini girin (mm). İşlem bu kadar öteye yapılır.\n"
                       "\"İki Nokta Seç\" ile delta ekranda iki tıklamayla belirlenir."),
        &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* form = new QFormLayout();
    auto* spinX = new QDoubleSpinBox(&dialog);
    auto* spinY = new QDoubleSpinBox(&dialog);
    auto* spinZ = new QDoubleSpinBox(&dialog);
    auto addRow = [&](const QString& name, QDoubleSpinBox* spin, double value) {
        spin->setRange(-1.0e6, 1.0e6);
        spin->setDecimals(2);
        spin->setSingleStep(10.0);
        spin->setSuffix(QStringLiteral(" mm"));
        spin->setMinimumWidth(170);
        spin->setValue(value);
        form->addRow(name, spin);
    };
    addRow(QStringLiteral("X"), spinX, delta.x);
    addRow(QStringLiteral("Y"), spinY, delta.y);
    addRow(QStringLiteral("Z"), spinZ, delta.z);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    auto* pickButton = new QPushButton(QStringLiteral("İki Nokta Seç"), &dialog);
    auto* cancelButton = new QPushButton(QStringLiteral("İptal"), &dialog);
    auto* okButton = new QPushButton(QStringLiteral("Tamam"), &dialog);
    okButton->setDefault(true);
    buttons->addWidget(pickButton);
    buttons->addStretch(1);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton);
    layout->addLayout(buttons);

    bool pick = false;
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(pickButton, &QPushButton::clicked, &dialog, [&dialog, &pick]() {
        pick = true;
        dialog.accept();
    });

    spinX->setFocus();
    spinX->selectAll();
    if (dialog.exec() != QDialog::Accepted) return false;
    pickTwoPoints = pick;
    if (!pick) delta = Vec3{spinX->value(), spinY->value(), spinZ->value()};
    return true;
}

} // namespace

QtMainWindow::QtMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , app_(GetModuleHandleW(nullptr))
{
    // Baslik damgasi: hangi derlemenin acik oldugu + OCC'nin derlenip
    // derlenmedigi tek bakista gorunur (mm::occStatus build_info.hpp'den).
    setWindowTitle(QString("Model Maker — Professional Wireframe CAD  [%1]  [%2]")
                       .arg(mm::buildStamp())
                       .arg(mm::occStatus()));
    resize(1400, 900);

    createMenus();
    createToolbar();
    createDockPanels();

    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    QObject::connect(undoShortcut, &QShortcut::activated, this, [this]() { app_.undo(); });
    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    QObject::connect(redoShortcut, &QShortcut::activated, this, [this]() { app_.redo(); });
    auto* newShortcut = new QShortcut(QKeySequence::New, this);
    QObject::connect(newShortcut, &QShortcut::activated, this, [this]() { app_.newDocument(); });
    // Alt+1..4: 3B gorsel stiller (wireframe / yarim-saydam / hidden / solid)
    auto* wireframeShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+1")), this);
    QObject::connect(wireframeShortcut, &QShortcut::activated, this, [this]() { app_.setVisualStyle(mm::VisualStyle::Wireframe); });
    auto* transparentShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+2")), this);
    QObject::connect(transparentShortcut, &QShortcut::activated, this, [this]() { app_.setVisualStyle(mm::VisualStyle::Transparent); });
    auto* hiddenShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+3")), this);
    QObject::connect(hiddenShortcut, &QShortcut::activated, this, [this]() { app_.setVisualStyle(mm::VisualStyle::HiddenLine); });
    auto* solidShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+4")), this);
    QObject::connect(solidShortcut, &QShortcut::activated, this, [this]() { app_.setVisualStyle(mm::VisualStyle::Solid); });
    auto* openShortcut = new QShortcut(QKeySequence::Open, this);
    QObject::connect(openShortcut, &QShortcut::activated, this, [this]() { app_.openDocument(); });
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    QObject::connect(saveShortcut, &QShortcut::activated, this, [this]() { app_.saveDocument(); });

    // Parametre istegi: Qt edit box (QInputDialog) — parametreli komutlar
    // (Offset/Fillet/Dizi/Divide) sayisal degeri buradan alir.
    app_.setParameterRequest([this](const std::wstring& prompt, const std::wstring& initial,
                                    std::wstring& out) {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Parametre"),
            QString::fromStdWString(prompt), QLineEdit::Normal,
            QString::fromStdWString(initial), &ok);
        if (!ok) return false;
        out = text.toStdWString();
        return true;
    });
    // DOGRUSAL KOPYA/TASIMA: komut baslayinca X/Y/Z delta paneli acilir
    // (kullanici talebi). Panel "Iki Nokta Sec" derse Application iki-tiklama
    // moduna gecer ve delta 2. nokta - 1. nokta olarak uygulanir.
    app_.setLinearDeltaRequest([this](Vec3& delta, bool& pickTwoPoints) {
        return promptLinearDeltaDialog(this, delta, pickTwoPoints);
    });
    // Bolt tool penceresi (Phase B1): bolt display'ine cift tik -> diyalog.
    app_.setBoltEditRequest([this](const mm::BoltGroup& bolt) {
        QTimer::singleShot(0, this, [this, bolt]() { openBoltPropertiesDialog(bolt); });
    });
    // End Plate alt nesnesine (levha/bulon) cift tik -> parametre diyalogu.
    // OK -> setSelectedEndPlateConnection (undo'lu yeniden uretim).
    app_.setEndPlateEditRequest([this](const mm::EndPlateConnection& conn) {
        QTimer::singleShot(0, this, [this, conn]() {
            mm::EndPlateParams p = conn.params;
            if (!openEndPlateParamsDialog(p, p)) return;
#ifdef MM_HAS_OCC
            app_.setSelectedEndPlateConnection(conn.id, p);
#endif
        });
    });
    // Universal parametrik component (Fin Plate vb.) alt nesnesine cift tik ->
    // registry parametre semasindan uretilen diyalog; OK -> recompute.
    app_.setComponentEditRequest([this](const mm::ComponentInstanceRecord& rec) {
        QTimer::singleShot(0, this, [this, rec]() {
            openComponentParamsDialog(rec);
        });
    });
    // Iki noktali gorunus: Application iki noktayi secince yeni MDI penceresi.
    // Kuyruklanir: secim fare olayi isleyicisinin icinden donmeden pencere
    // olusturulmasin (yeniden giris / odak sorunlari olmasin).
    app_.setTwoPointViewCallback([this](const ViewDefinition& view) {
        QTimer::singleShot(0, this, [this, view]() { openTwoPointView(view); });
    });
    // Qt kisayollari/menuleri (Ctrl+Z, ribbon komutlari...) tuval mesaji
    // disindan belgeyi degistirir; diger gorunusleri tazele (ucuz imza).
    auto* viewSyncTimer = new QTimer(this);
    viewSyncTimer->setInterval(120);
    QObject::connect(viewSyncTimer, &QTimer::timeout, this, [this]() { app_.syncPassiveViewports(); });
    viewSyncTimer->start();

    // MDI: merkez alan QMdiArea; ana model gorunusu kapatilamaz bir alt
    // pencere, iki noktali gorunusler yanina yeni alt pencereler olarak acilir.
    // SIRA KRITIK: canvasContainer_ alt pencereye winId()'den ONCE yerlesir —
    // native bir widget sonradan yeniden ebeveynlenirse Qt HWND'sini yeniden
    // olusturabilir ve icine SetParent ile gomulu Win32 canvas yok olur.
    mdiArea_ = new QMdiArea(this);
    mdiArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdiArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mdiArea_->setBackground(QBrush(QColor(45, 45, 48)));
    setCentralWidget(mdiArea_);
    canvasContainer_ = new QWidget;
    modelSubWindow_ = mdiArea_->addSubWindow(canvasContainer_,
        Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint);
    modelSubWindow_->setWindowTitle(QStringLiteral("Model (3B)"));
    modelSubWindow_->setOption(QMdiSubWindow::RubberBandResize, true);
    modelSubWindow_->installEventFilter(this); // kapatma engeli
    modelSubWindow_->showMaximized();
    canvasContainer_->installEventFilter(this);

    // Create Application's window, then embed its canvas
    app_.createMainWindow(SW_HIDE);

    HWND canvas = app_.canvasHandle();
    HWND appWnd = app_.windowHandle();

    // Reparent canvas to our widget
    canvasContainer_->winId(); // force native handle
    HWND qtHwnd = reinterpret_cast<HWND>(canvasContainer_->winId());
    if (!SetParent(canvas, qtHwnd)) {
        // Farkli DPI-awareness'li HWND'lerde SetParent basarisiz olabilir.
        // PMv2 surec farkindaligi (main.cpp) canvas'i da PMv2 yaptigi icin
        // normalde buraya dusulmez; duserse teshis icin logla.
        FILE* diag = fopen("model-maker-resize.log", "a");
        if (diag) { fprintf(diag, "SetParent-FAILED err=%lu\n", GetLastError()); fclose(diag); }
    }
    // Qt kendi arka deposunu boyarken child canvas alanını ezecektir;
    // WS_CLIPCHILDREN konteynerin canvas bölgesine hiç dokunmamasını sağlar.
    SetWindowLongPtrW(qtHwnd, GWL_STYLE,
                      GetWindowLongPtrW(qtHwnd, GWL_STYLE) | WS_CLIPCHILDREN);
    ShowWindow(appWnd, SW_HIDE);

    RECT rc;
    GetClientRect(qtHwnd, &rc);
    SetWindowPos(canvas, nullptr, 0, 0, rc.right, rc.bottom,
                 SWP_NOZORDER | SWP_SHOWWINDOW);

    statusBar()->showMessage("Hazır");
    // Application'in updateStatus metni canli olarak Qt durum cubuguna akar
    // (GPU modu gostergesi dahil — GDI STATIC status cubugu Qt'de gorunmez).
    app_.setStatusCallback([this](const std::wstring& text) {
        statusBar()->showMessage(QString::fromStdWString(text));
    });

    // DOSYA ILERLEMESI: dosya ac/kaydet sirasinda ekranin ORTASINDA ilerleme penceresi.
    // Application yalniz bildirir (asama + adim); pencere burada sahiplenilir.
    // Kisa islemlerde pencere bir an gorunup kapanabilir — kabul edilen davranis.
    auto progress = std::make_shared<mm::ui::ProgressOverlay>(this);
    {
        Application::IoProgressSink sink;
        sink.begin = [progress](const std::string& title, int total) {
            progress->begin(QString::fromStdString(title), total);
        };
        sink.update = [progress](int done, int total, const std::string& detail) {
            progress->report(done, total, QString::fromStdString(detail));
        };
        sink.end = [progress]() { progress->finish(); };
        app_.setIoProgressSink(std::move(sink));
    }
}

QtMainWindow::~QtMainWindow() {
    // app_ (uye) alt pencerelerden ONCE yok edilir (QWidget cocuklari taban
    // sinif yikicisinda silinir). Gorunusleri belgeden simdi ayir.
    if (mdiArea_) {
        for (QMdiSubWindow* sub : mdiArea_->subWindowList())
            if (QWidget* holder = sub->widget())
                for (QWidget* child : holder->findChildren<QWidget*>())
                    if (auto* tpv = dynamic_cast<TwoPointViewWidget*>(child)) tpv->detach();
    }
}

void QtMainWindow::openTwoPointView(const ViewDefinition& definition) {
    if (!mdiArea_) return;
    ViewDefinition view = definition;
    const bool plan = isPlanView(view);
    view.name = (plan ? L"Plan Görünüşü " : L"Görünüş ") + std::to_wstring(++twoPointViewCounter_);
    // SIRA KRITIK: widget alt pencereye winId()/tuval olusturulmadan ONCE
    // yerlesmeli (sonradan yeniden ebeveynlenen native widget HWND'sini
    // yeniden uretir ve icindeki Win32 tuvali kaybolur). Bu yuzden once
    // bos kap eklenir, tuval kap native iken icinde uretilir.
    auto* holder = new QWidget;
    QMdiSubWindow* sub = mdiArea_->addSubWindow(holder);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* widget = new TwoPointViewWidget(app_, view, holder);
    layout->addWidget(widget);
    if (!widget->isValid()) {
        sub->close();
        QMessageBox::warning(this, QStringLiteral("Görünüş"),
                             QStringLiteral("Görünüş penceresi oluşturulamadı."));
        return;
    }
    // Pencere sistem menusu: sigdir + derinlik (tuval sag tiki komutlara ait).
    if (QMenu* systemMenu = sub->systemMenu()) {
        systemMenu->addSeparator();
        systemMenu->addAction(QStringLiteral("Sığdır"), widget, [widget]() { widget->fitView(); });
        systemMenu->addAction(QStringLiteral("Görünüş derinliği..."), widget,
                              [widget]() { widget->editDepth(); });
    }
    // Baslik: plan gorunusunde UZUNLUK yok (P1-P2 hatti yok) — bunun yerine
    // gorunusun Z kotu yazilir; elevasyonda eski baslik korunur.
    sub->setWindowTitle(QString::fromStdWString(view.name) +
                        (plan ? QStringLiteral("  —  plan, Z = %1 mm").arg(std::lround(view.origin.z))
                              : QStringLiteral("  —  %1 mm, XY'ye dik").arg(std::lround(view.length))));
    // Gorunus penceresi tum MDI alanini kaplasin (Tekla gibi tek gorunus).
    // MDI'da ayni anda tek pencere buyutulmus olabilir; bu, varsa buyutulmus
    // ana gorunusu otomatik normale dondurur.
    sub->showMaximized();
    mdiArea_->setActiveSubWindow(sub);
    // Kapatilinca (X / Ctrl+F4) kalan tek pencere = ana gorunus -> yeniden
    // buyutulur, aksi halde ana pencere normal boyutta asili kalir.
    QObject::connect(sub, &QObject::destroyed, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            bool anyView = false;
            for (QMdiSubWindow* s : mdiArea_->subWindowList())
                if (s != modelSubWindow_) { anyView = true; break; }
            if (!anyView && modelSubWindow_ && !modelSubWindow_->isMaximized())
                modelSubWindow_->showMaximized();
        });
    });
    widget->setFocus();
    QTimer::singleShot(0, widget, [widget]() { widget->fitView(); });
    if (HWND canvas = widget->canvasHandle()) SetFocus(canvas);
    statusBar()->showMessage(QString::fromStdWString(view.name) + QStringLiteral(" açıldı"), 4000);
}

void QtMainWindow::closeAllTwoPointViews() {
    if (!mdiArea_) return;
    for (QMdiSubWindow* sub : mdiArea_->subWindowList())
        if (sub != modelSubWindow_) sub->close();
    if (modelSubWindow_) modelSubWindow_->showMaximized();
}

void QtMainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // QResizeEvent native HWND boyutu guncellenmeden ONCE gelebilir; o anda
    // GetClientRect bayat (eski kucuk) deger doner ve gomulu tuval yarim
    // boyutta kalir. Olcumu bir sonraki olay dongusu turuna ertele.
    QTimer::singleShot(0, this, [this]() { resizeEmbeddedCanvas(); });
}

void QtMainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // The central widget only reaches its final size during the first layout
    // pass after the window is shown; without this the embedded GDI canvas
    // keeps the tiny pre-layout rect from the constructor (top-left corner).
    // AYRICA: constructor'daki showMaximized() MDI alani henuz boyutlanmadan
    // calistigi icin kaybolabiliyor — model alt penceresi varsayilan (kucuk)
    // boyutta kalir ve tuval "yarim pencere" gorunur. Gosterimden sonra
    // maximize'i yeniden uygula, olcumu bir tur daha ertele.
    QTimer::singleShot(0, this, [this]() {
        if (modelSubWindow_ && !modelSubWindow_->isMaximized())
            modelSubWindow_->showMaximized();
        QTimer::singleShot(0, this, [this]() { resizeEmbeddedCanvas(); });
    });
    // VARSAYILAN BASLANGIC (kullanici talebi): GL + 3B + Solid — Application::
    // run()'daki ayni kod Qt kabugunda hic calismiyordu (Win32 mesaj dongusu
    // kullanilmiyor); tek kez, ilk gorunurlukte uygula.
    if (!startupDefaultsApplied_) {
        startupDefaultsApplied_ = true;
        QTimer::singleShot(0, this, [this]() {
            // 3B'ye gec (mode_ View3D) + SpaceMouse baslat. toggle3DView icinde
            // zaten ensureSpaceMouseStarted cagrilir (mode_ View3D olunca).
            // 3B + Solid + GL (canvas boyutu gecerliyse simdi, degilse ilk
            // gecerli resize'da; basarisiz/onceki acilis donduysa GDI).
            app_.applyStartupDefaults3D();
            app_.ensureSpaceMouseStarted();       // her durumda otomatik baslat
        });
    }
    if (!profileUiLogged_ && profileSelector_) {
        profileUiLogged_ = true;
        // Log after Qt's first layout pass.  This describes the widget the
        // user actually sees, not the deliberately hidden Win32 host window.
        QTimer::singleShot(0, this, [this]() {
            if (!profileSelector_) return;
            const QPoint topLeft = profileSelector_->mapToGlobal(QPoint(0, 0));
            const QByteArray current = profileSelector_->currentText().toUtf8();
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) {
                fprintf(diag,
                        "PROFILE-QT-PICKER visible=%d enabled=%d items=%d "
                        "rect=(%d,%d)-(%d,%d) current=%s\n",
                        profileSelector_->isVisibleTo(this) ? 1 : 0,
                        profileSelector_->isEnabled() ? 1 : 0,
                        profileSelector_->count(), topLeft.x(), topLeft.y(),
                        topLeft.x() + profileSelector_->width(),
                        topLeft.y() + profileSelector_->height(), current.constData());
                fclose(diag);
            }
        });
    }
}

bool QtMainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == canvasContainer_ && event->type() == QEvent::Resize) {
        // QResizeEvent native HWND boyutu guncellenmeden ONCE gelebilir; o anda
        // GetClientRect bayat deger doner ve gomulu tuval yarim boyutta kalir.
        // Olcumu bir sonraki olay dongusu turuna ertele (WM_SIZE islenip native
        // boyut kesinlestikten sonra GetClientRect dogru olcuyu verir).
        QTimer::singleShot(0, this, [this]() { resizeEmbeddedCanvas(); });
    }
    // Ana model alt penceresi kapatilamaz (Ctrl+F4 / sistem menusu dahil).
    if (watched == modelSubWindow_ && event->type() == QEvent::Close) {
        event->ignore();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void QtMainWindow::resizeEmbeddedCanvas() {
    HWND canvas = app_.canvasHandle();
    if (canvas && canvasContainer_) {
        RECT rc;
        GetClientRect(reinterpret_cast<HWND>(canvasContainer_->winId()), &rc);
        SetWindowPos(canvas, nullptr, 0, 0, rc.right, rc.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Diagnostik (ilk 20 cagri): Qt mantiksal boyut ile native istemci
        // alanini karsilastir. Ikisi de yarim kaliyorsa MDI yerlesimi sucludur;
        // qt buyuk/client kucuk kalıyorsa bayat GetClientRect (erteleme cozer).
        // cvOuter/cvClient: gomulu Win32 tuvalin DIS (GetWindowRect) ve IC
        // (GetClientRect) boyutlari. DPI uyusmazligi: cvClient, cvOuter'dan
        // ~%20 kucuk kalir (virtualization). WS_EX_CLIENTEDGE kenarligi yalniz
        // bir kac px'dir; buyuk fark = DPI, kucuk fark = kenarlik.
        static int diagCount = 0;
        if (diagCount < 20) {
            ++diagCount;
            RECT cvClient{};
            GetClientRect(canvas, &cvClient);
            RECT cvOuter{};
            GetWindowRect(canvas, &cvOuter);
            const LONG cvOuterW = cvOuter.right - cvOuter.left;
            const LONG cvOuterH = cvOuter.bottom - cvOuter.top;
            FILE* f = fopen("model-maker-resize.log", "a");
            if (f) {
                fprintf(f, "resize#%d qt=(%d,%d) host=(%ld,%ld) cvOuter=(%ld,%ld) cvClient=(%ld,%ld) sub=(%d,%d) max=%d\n",
                        diagCount, canvasContainer_->width(), canvasContainer_->height(),
                        rc.right, rc.bottom, cvOuterW, cvOuterH, cvClient.right, cvClient.bottom,
                        modelSubWindow_ ? modelSubWindow_->width() : -1,
                        modelSubWindow_ ? modelSubWindow_->height() : -1,
                        (modelSubWindow_ && modelSubWindow_->isMaximized()) ? 1 : 0);
                fclose(f);
            }
        }
    }
    // Baslangic GL'i canvas 0x0 iken ertelendiyse simdi dene (tek seferlik).
    app_.tryEnableStartupGpu();
}

// ===========================================================================
// TEK KAYNAK KOMUT TABLOSU (menü == ribbon)
// ---------------------------------------------------------------------------
// Her komut kategorisi (Çizim, Düzenle, Çelik, ...) BURADA bir kez tanımlanır.
// createMenus() aynı tablodan "☰ Menü" altındaki menüyü, createToolbar() aynı
// tablodan ribbon sekmesini ve Quick Launch listesini üretir. Böylece bir komut
// menüde olup ribbon'da olmayamaz, yanlış kategoriye düşemez. YENİ KOMUT
// EKLERKEN yalnız commandCategories() içine, doğru kategoriye ekle.
//   - tabTitle boş kategori yalnız menüde görünür (Dosya, Pencere).
//   - uiGroup: menüde alt menü, ribbon'da açılır (InstantPopup) düğme.
//   - uiSep: menüde ayırıcı, ribbon'da ince dikey çizgi.
// ===========================================================================
namespace {

struct UiCommand {
    QString menuText;    // menüdeki ad (& = mnemonik)
    QString ribbonText;  // ribbon düğmesinin kısa adı
    QString tip;         // ribbon araç ipucu
    ToolGlyph glyph{ToolGlyph::Extents};
    std::function<void()> run;
    QString shortcut;               // gösterilen kısayol
    bool bindShortcut{false};       // true: menü eylemine QKeySequence bağlanır
    std::function<bool()> checked;  // doluysa menüde onay işaretli eylem
};

struct UiEntry {
    enum class Kind { Command, Separator, Group };
    Kind kind{Kind::Command};
    UiCommand cmd;                  // Command: komutun kendisi; Group: başlık
    std::vector<UiEntry> children;  // Group: alt menü / açılır düğme içeriği
};

UiEntry uiCmd(const QString& menu, const QString& ribbon, const QString& tip,
              ToolGlyph glyph, std::function<void()> run) {
    UiEntry e;
    e.cmd.menuText = menu;
    e.cmd.ribbonText = ribbon;
    e.cmd.tip = tip;
    e.cmd.glyph = glyph;
    e.cmd.run = std::move(run);
    return e;
}

UiEntry uiKey(UiEntry e, const QString& shortcut, bool bind) {
    e.cmd.shortcut = shortcut;
    e.cmd.bindShortcut = bind;
    return e;
}

UiEntry uiSep() {
    UiEntry e;
    e.kind = UiEntry::Kind::Separator;
    return e;
}

UiEntry uiGroup(const QString& menu, const QString& ribbon, const QString& tip,
                ToolGlyph glyph, std::vector<UiEntry> children) {
    UiEntry e;
    e.kind = UiEntry::Kind::Group;
    e.cmd.menuText = menu;
    e.cmd.ribbonText = ribbon;
    e.cmd.tip = tip;
    e.cmd.glyph = glyph;
    e.children = std::move(children);
    return e;
}

// "&Taşı\tCtrl+X" -> "Taşı" (Quick Launch ve araç ipucu için düz ad).
QString plainCommandLabel(QString s) {
    const int tab = s.indexOf(QLatin1Char('\t'));
    if (tab >= 0) s.truncate(tab);
    s.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    s.remove(QLatin1Char('&'));
    s.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    if (s.endsWith(QStringLiteral("..."))) s.chop(3);
    return s.trimmed();
}

// Menü eylemi: metin + kısayol + ikon + onay durumu tek yerde.
QAction* addUiAction(QMenu* menu, QObject* ctx, const UiCommand& c, bool withIcon) {
    QString text = c.menuText;
    if (!c.shortcut.isEmpty() && !c.bindShortcut) text += QStringLiteral("\t") + c.shortcut;
    QAction* act = menu->addAction(text, ctx, c.run);
    if (withIcon) act->setIcon(makeToolIcon(c.glyph));
    if (c.bindShortcut && !c.shortcut.isEmpty()) act->setShortcut(QKeySequence(c.shortcut));
    if (!c.tip.isEmpty()) act->setToolTip(c.tip);
    if (c.checked) {
        act->setCheckable(true);
        act->setChecked(c.checked());
        // F7 vb. ile menü dışından değişebilir: menü her açılışta yeniden okur.
        QObject::connect(menu, &QMenu::aboutToShow, act,
                         [act, get = c.checked]() { act->setChecked(get()); });
    }
    return act;
}

void fillUiMenu(QMenu* menu, QObject* ctx, const std::vector<UiEntry>& entries, bool withIcons) {
    for (const UiEntry& e : entries) {
        switch (e.kind) {
        case UiEntry::Kind::Separator:
            menu->addSeparator();
            break;
        case UiEntry::Kind::Command:
            addUiAction(menu, ctx, e.cmd, withIcons);
            break;
        case UiEntry::Kind::Group: {
            QMenu* sub = menu->addMenu(e.cmd.menuText);
            if (withIcons) sub->setIcon(makeToolIcon(e.cmd.glyph));
            fillUiMenu(sub, ctx, e.children, false);
            break;
        }
        }
    }
}

void collectQuickCommands(const std::vector<UiEntry>& entries,
                          std::vector<std::pair<QString, std::function<void()>>>& out) {
    for (const UiEntry& e : entries) {
        if (e.kind == UiEntry::Kind::Command && e.cmd.run)
            out.emplace_back(plainCommandLabel(e.cmd.menuText), e.cmd.run);
        else if (e.kind == UiEntry::Kind::Group)
            collectQuickCommands(e.children, out);
    }
}

} // namespace

struct QtMainWindow::CommandCategory {
    QString menuTitle;   // "☰ Menü" altındaki başlık (& = mnemonik)
    QString tabTitle;    // ribbon sekmesi; boş = yalnız menü
    std::vector<UiEntry> entries;
};

std::vector<QtMainWindow::CommandCategory> QtMainWindow::commandCategories() {
    std::vector<CommandCategory> cats;

    // ---- DOSYA (yalnız menü) ----
    cats.push_back({QStringLiteral("&Dosya"), QString(), {
        uiKey(uiCmd(QStringLiteral("&Yeni"), QStringLiteral("Yeni"), QStringLiteral("Yeni belge"), ToolGlyph::New,
              [this]() { app_.newDocument(); }), QStringLiteral("Ctrl+N"), false),
        uiKey(uiCmd(QStringLiteral("&Aç..."), QStringLiteral("Aç"), QStringLiteral("Belge aç"), ToolGlyph::Open,
              [this]() { app_.openDocument(); }), QStringLiteral("Ctrl+O"), false),
        uiKey(uiCmd(QStringLiteral("&Kaydet"), QStringLiteral("Kaydet"), QStringLiteral("Belgeyi kaydet"), ToolGlyph::Save,
              [this]() { app_.saveDocument(); }), QStringLiteral("Ctrl+S"), false),
        uiCmd(QStringLiteral("Farklı Kay&det..."), QStringLiteral("Farklı Kaydet"), QStringLiteral("Farklı kaydet"), ToolGlyph::Save,
              [this]() { app_.saveDocumentAs(); }),
        uiSep(),
        uiCmd(QStringLiteral("DXF &İçe Aktar..."), QStringLiteral("DXF Al"), QStringLiteral("DXF içe aktar"), ToolGlyph::Import,
              [this]() { app_.importDxf(); }),
        uiCmd(QStringLiteral("DXF D&ışa Aktar..."), QStringLiteral("DXF Ver"), QStringLiteral("DXF dışa aktar"), ToolGlyph::Export,
              [this]() { app_.exportDxf(); }),
        uiSep(),
        uiCmd(QStringLiteral("Çı&kış"), QStringLiteral("Çıkış"), QStringLiteral("Uygulamadan çık"), ToolGlyph::Delete,
              []() { QApplication::quit(); }),
    }});

    // ---- ÇİZİM ----
    cats.push_back({QStringLiteral("Çi&zim"), QStringLiteral("Çizim"), {
        uiCmd(QStringLiteral("Çiz&gi"), QStringLiteral("Çizgi"), QStringLiteral("Çizgi"), ToolGlyph::Line,
              [this]() { app_.selectTool(DrawTool::Line); }),
        uiCmd(QStringLiteral("&Polyline"), QStringLiteral("Polyline"), QStringLiteral("Polyline"), ToolGlyph::Polyline,
              [this]() { app_.selectTool(DrawTool::Polyline); }),
        uiCmd(QStringLiteral("Dikdört&gen"), QStringLiteral("Dikdörtgen"), QStringLiteral("Dikdörtgen"), ToolGlyph::Rect,
              [this]() { app_.selectTool(DrawTool::Rectangle); }),
        uiCmd(QStringLiteral("Dai&re"), QStringLiteral("Daire"), QStringLiteral("Daire"), ToolGlyph::Circle,
              [this]() { app_.selectTool(DrawTool::Circle); }),
        uiCmd(QStringLiteral("&3DFACE"), QStringLiteral("3DFACE"), QStringLiteral("3B yüzey (3DFACE)"), ToolGlyph::Face3D,
              [this]() { app_.selectTool(DrawTool::Face3D); }),
        uiSep(),
        uiKey(uiCmd(QStringLiteral("Böl (&Divide)"), QStringLiteral("Böl"), QStringLiteral("İki nokta arasını eşit böl (D)"), ToolGlyph::LinearArray,
              [this]() { app_.activateDivide(); }), QStringLiteral("D"), false),
        uiSep(),
        // Paper-space çizim: seçili parça/assembly -> Ön/Üst/Yan görünüşlü A3 sayfa (PDF/JSON).
        uiCmd(QStringLiteral("&Paper Çizim (Seçimden)..."), QStringLiteral("Paper Çizim"),
              QStringLiteral("Seçili parça/assembly için Ön-Üst-Yan görünüşlü A3 çizim üret (PDF/JSON)"),
              ToolGlyph::Export,
              [this]() {
                  mm::drawing::showDrawingDialog(
                      this, app_.document(), app_.selectedModelIndices(),
                      app_.selectionMode() == mm::Application::SelectionMode::Assembly);
              }),
    }});

    // ---- DÜZENLE ----
    cats.push_back({QStringLiteral("Dü&zenle"), QStringLiteral("Düzenle"), {
        uiKey(uiCmd(QStringLiteral("&Geri Al"), QStringLiteral("Geri"), QStringLiteral("Geri al (Ctrl+Z)"), ToolGlyph::Undo,
              [this]() { app_.undo(); }), QStringLiteral("Ctrl+Z"), false),
        uiKey(uiCmd(QStringLiteral("&Yinele"), QStringLiteral("İleri"), QStringLiteral("Yinele (Ctrl+Y)"), ToolGlyph::Redo,
              [this]() { app_.redo(); }), QStringLiteral("Ctrl+Y"), false),
        uiCmd(QStringLiteral("&Pasif"), QStringLiteral("Pasif"), QStringLiteral("Tüm komutları bırak"), ToolGlyph::Pan,
              [this]() { app_.deactivateAllCommands(); }),
        uiSep(),
        uiCmd(QStringLiteral("&Taşı"), QStringLiteral("Taşı"), QStringLiteral("Taşı"), ToolGlyph::Move,
              [this]() { app_.startTransformCommand(TransformCommand::Move); }),
        uiCmd(QStringLiteral("&Kopyala"), QStringLiteral("Kopyala"), QStringLiteral("Kopyala"), ToolGlyph::Copy,
              [this]() { app_.startTransformCommand(TransformCommand::Copy); }),
        uiCmd(QStringLiteral("Doğrusal &Kopyala (ΔX/ΔY/ΔZ)"), QStringLiteral("Kopyala-Δ"),
              QStringLiteral("Doğrusal kopyala: seçim + iki nokta; hedefte \"dx,dy,dz\" yazılırsa X/Y/Z mesafesiyle kopyalar (çoklu)"),
              ToolGlyph::Copy,
              [this]() { app_.startTransformCommand(TransformCommand::CopyLinear); }),
        uiCmd(QStringLiteral("Doğrusal &Taşı (ΔX/ΔY/ΔZ)"), QStringLiteral("Taşı-Δ"),
              QStringLiteral("Doğrusal taşı: seçim + iki nokta; hedefte \"dx,dy,dz\" yazılırsa X/Y/Z mesafesiyle taşır"),
              ToolGlyph::Move,
              [this]() { app_.startTransformCommand(TransformCommand::MoveLinear); }),
        uiCmd(QStringLiteral("&Ofset"), QStringLiteral("Ofset"), QStringLiteral("Ofset"), ToolGlyph::Offset,
              [this]() { app_.startTransformCommand(TransformCommand::Offset); }),
        uiCmd(QStringLiteral("A&yna"), QStringLiteral("Ayna"), QStringLiteral("Ayna"), ToolGlyph::Mirror,
              [this]() { app_.startTransformCommand(TransformCommand::Mirror); }),
        uiCmd(QStringLiteral("&Sil"), QStringLiteral("Sil"), QStringLiteral("Sil"), ToolGlyph::Delete,
              [this]() { app_.startTransformCommand(TransformCommand::Delete); }),
        uiSep(),
        uiCmd(QStringLiteral("Doğrusal &Dizi"), QStringLiteral("Dizi"), QStringLiteral("Doğrusal dizi"), ToolGlyph::LinearArray,
              [this]() { app_.startTransformCommand(TransformCommand::LinearArray); }),
        uiCmd(QStringLiteral("Dairesel Di&zi"), QStringLiteral("D.Dizi"), QStringLiteral("Dairesel dizi"), ToolGlyph::PolarArray,
              [this]() { app_.startTransformCommand(TransformCommand::PolarArray); }),
        uiSep(),
        uiCmd(QStringLiteral("T&rim"), QStringLiteral("Trim"), QStringLiteral("Trim"), ToolGlyph::Trim,
              [this]() { app_.startTransformCommand(TransformCommand::Trim); }),
        uiCmd(QStringLiteral("E&xtend"), QStringLiteral("Extend"), QStringLiteral("Extend"), ToolGlyph::Extend,
              [this]() { app_.startTransformCommand(TransformCommand::Extend); }),
        uiCmd(QStringLiteral("&Fillet"), QStringLiteral("Fillet"), QStringLiteral("Fillet"), ToolGlyph::Fillet,
              [this]() { app_.startTransformCommand(TransformCommand::Fillet); }),
    }});

    // ---- ÇELİK: bağlantılar (bulon, kaynak, levhalar) + component kataloğu
    //      + Tekla Component Bridge ----
    {
        std::vector<UiEntry> steel = {
            uiCmd(QStringLiteral("&Bolt (Bulon)"), QStringLiteral("Bolt"), QStringLiteral("İki parçayı bulonla birleştir"), ToolGlyph::Bolt,
                  [this]() { app_.startBoltCommand(); }),
            uiCmd(QStringLiteral("&Kaynak (Weld)..."), QStringLiteral("Kaynak"), QStringLiteral("İki parça arasına kaynak"), ToolGlyph::Fillet,
                  [this]() { startWeldCommand(); }),
            uiSep(),
            uiCmd(QStringLiteral("&End Plate (Alın Levhası)"), QStringLiteral("End Plate"), QStringLiteral("Alın levhası bağlantısı (kolon+kiriş)"), ToolGlyph::Plate,
                  [this]() {
                      mm::EndPlateParams p = app_.pendingEndPlateParams();
                      if (!openEndPlateParamsDialog(p, p)) return;
                      app_.setPendingEndPlateParams(p);
                      app_.startEndPlateCommand();
                  }),
            uiCmd(QStringLiteral("&Fin Plate (Kayma Levhası)"), QStringLiteral("Fin Plate"), QStringLiteral("Kayma levhası bağlantısı (kolon+kiriş)"), ToolGlyph::Plate,
                  [this]() {
                      // Parametrik component: varsayılan şema parametreleriyle başla.
                      app_.setPendingFinPlateParams({});
                      app_.startFinPlateCommand();
                  }),
        };

        // GENEL COMPONENT KATALOĞU (C7): registry + dialog kayıtlarından ÜRETİLİR;
        // yeni component için buraya kod eklenmez. End Plate catalogVisible=false.
        const auto catalog = mm::buildComponentCatalog(
            mm::ComponentRegistry::instance(), mm::ComponentDialogRegistry::instance());
        if (!catalog.empty()) {
            std::vector<UiEntry> items;
            for (const mm::CatalogEntry& ce : catalog) {
                QString label = QString::fromStdWString(ce.displayName);
                if (!ce.sourceComponentNumber.empty()) {
                    const QString suffix = QStringLiteral(" (") +
                        QString::fromStdWString(ce.sourceComponentNumber) +
                        QStringLiteral(")");
                    if (!label.endsWith(suffix)) label += suffix;
                }
                QString tip = QString::fromStdWString(ce.category);
                if (!ce.coverageStatus.empty())
                    tip += QStringLiteral(" — ") + QString::fromStdWString(ce.coverageStatus);
                items.push_back(uiCmd(label, label, tip, ToolGlyph::Plate,
                    [this, typeId = ce.typeId]() {
                        app_.setPendingComponentParams(standardComponentParams(typeId));
                        app_.startComponentCommand(typeId);
                    }));
            }
            steel.push_back(uiGroup(QStringLiteral("Component &Kataloğu"), QStringLiteral("Katalog"),
                                    QStringLiteral("Component kataloğu"), ToolGlyph::Plate, std::move(items)));
        }

        steel.push_back(uiSep());
        steel.push_back(uiGroup(QStringLiteral("&Tekla Component Bridge"), QStringLiteral("Tekla Bridge"),
            QStringLiteral("Tekla'dan .mmbridge paketi içe aktar / dönüştür / kataloğa kaydet"), ToolGlyph::Import, {
            uiCmd(QStringLiteral("&İçe Aktar..."), QStringLiteral("İçe Aktar"), QStringLiteral("Tekla Component Bridge içe aktar"), ToolGlyph::Import,
                  [this]() { importTeklaBridge(); }),
            uiCmd(QStringLiteral("&Native Component'e Dönüştür..."), QStringLiteral("Native"), QStringLiteral("Bridge paketini native component'e dönüştür"), ToolGlyph::Import,
                  [this]() { importTeklaBridgeNative(); }),
            uiCmd(QStringLiteral("&Kataloğa Şablon Kaydet..."), QStringLiteral("Şablon"), QStringLiteral("Bridge paketini katalog şablonu olarak kaydet"), ToolGlyph::Import,
                  [this]() { saveTeklaBridgeTemplate(); }),
        }));

        cats.push_back({QStringLiteral("Çe&lik"), QStringLiteral("Çelik"), std::move(steel)});
    }

    // ---- KONSTRÜKSİYON & KESME ----
    cats.push_back({QStringLiteral("K&onstrüksiyon"), QStringLiteral("Konstrüksiyon"), {
        uiCmd(QStringLiteral("Konstrüksiyon &Düzlemi (3 Nokta)"), QStringLiteral("K. Düzlem"), QStringLiteral("Konstrüksiyon düzlemi (3 nokta)"), ToolGlyph::Plane,
              [this]() { app_.startConstructionPlaneCommand(); }),
        uiCmd(QStringLiteral("Konstrüksiyon &Nesnelerini Göster/Gizle"), QStringLiteral("Gizle/Göster"), QStringLiteral("Konstrüksiyon nesnelerini göster/gizle"), ToolGlyph::Toggle3D,
              [this]() { app_.toggleConstructionVisibility(); }),
        uiSep(),
        uiCmd(QStringLiteral("&Fitting (Uç Kesme)"), QStringLiteral("Fitting"), QStringLiteral("Fitting (uç kesme)"), ToolGlyph::Trim,
              [this]() { app_.startFittingCommand(); }),
        uiCmd(QStringLiteral("&Plane Cut"), QStringLiteral("Plane Cut"), QStringLiteral("Düzlemle kesme"), ToolGlyph::Plane,
              [this]() { app_.startPlaneCutCommand(); }),
        uiCmd(QStringLiteral("&Line Cut (Çizgi)"), QStringLiteral("Line Cut"), QStringLiteral("Çizgiyle kesme"), ToolGlyph::Line,
              [this]() { app_.startLineCutCommand(); }),
        uiCmd(QStringLiteral("P&olygon Cut (3DFACE)"), QStringLiteral("Polygon Cut"), QStringLiteral("3DFACE ile kesme"), ToolGlyph::Face3D,
              [this]() { app_.startPolygonCutCommand(); }),
        uiCmd(QStringLiteral("P&art Cut (Parça)"), QStringLiteral("Part Cut"), QStringLiteral("Parça ile kesme"), ToolGlyph::Delete,
              [this]() { app_.startPartCutCommand(); }),
    }});

    // ---- GÖRÜNÜM: kamera/navigasyon + standart yönler ----
    cats.push_back({QStringLiteral("G&örünüm"), QStringLiteral("Görünüm"), {
        uiCmd(QStringLiteral("2&B / 3B"), QStringLiteral("2B/3B"), QStringLiteral("2B / 3B görünüm"), ToolGlyph::Toggle3D,
              [this]() { app_.toggle3DView(); }),
        uiCmd(QStringLiteral("&Zoom Extents"), QStringLiteral("Extents"), QStringLiteral("Zoom Extents"), ToolGlyph::Extents,
              [this]() { app_.zoomExtents2D(); }),
        uiCmd(QStringLiteral("Zoom &Window"), QStringLiteral("Zoom Penc."), QStringLiteral("Zoom Window"), ToolGlyph::Extents,
              [this]() { app_.startZoomWindow2D(); }),
        uiKey(uiCmd(QStringLiteral("Görünümü &Sıfırla"), QStringLiteral("Sıfırla"), QStringLiteral("Görünümü sıfırla (Ctrl+R)"), ToolGlyph::Reset,
              [this]() { app_.resetView(); }), QStringLiteral("Ctrl+R"), true),
        uiSep(),
        uiGroup(QStringLiteral("Görü&nüş Yönü"), QStringLiteral("Görünüş"), QStringLiteral("Standart görünüş yönleri (Ctrl+1..7)"), ToolGlyph::Extents, {
            uiKey(uiCmd(QStringLiteral("Ön&den"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Front); }), QStringLiteral("Ctrl+1"), true),
            uiKey(uiCmd(QStringLiteral("Ar&kadan"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Back); }), QStringLiteral("Ctrl+2"), true),
            uiKey(uiCmd(QStringLiteral("&Soldan"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Left); }), QStringLiteral("Ctrl+3"), true),
            uiKey(uiCmd(QStringLiteral("S&ağdan"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Right); }), QStringLiteral("Ctrl+4"), true),
            uiSep(),
            uiKey(uiCmd(QStringLiteral("Üs&tten"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Top); }), QStringLiteral("Ctrl+5"), true),
            uiKey(uiCmd(QStringLiteral("A&lttan"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Bottom); }), QStringLiteral("Ctrl+6"), true),
            uiSep(),
            uiKey(uiCmd(QStringLiteral("&İzometrik"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.setStandardView(StandardView::Isometric); }), QStringLiteral("Ctrl+7"), true),
        }),
        uiGroup(QStringLiteral("Görsel &Stil"), QStringLiteral("Stil"), QStringLiteral("3B görsel stil (Alt+1..4)"), ToolGlyph::Toggle3D, {
            uiKey(uiCmd(QStringLiteral("&Wireframe"), QString(), QString(), ToolGlyph::Toggle3D,
                  [this]() { app_.setVisualStyle(mm::VisualStyle::Wireframe); }), QStringLiteral("Alt+1"), false),
            uiKey(uiCmd(QStringLiteral("&Yarı Saydam"), QString(), QString(), ToolGlyph::Toggle3D,
                  [this]() { app_.setVisualStyle(mm::VisualStyle::Transparent); }), QStringLiteral("Alt+2"), false),
            uiKey(uiCmd(QStringLiteral("&Gizli Çizgi"), QString(), QString(), ToolGlyph::Toggle3D,
                  [this]() { app_.setVisualStyle(mm::VisualStyle::HiddenLine); }), QStringLiteral("Alt+3"), false),
            uiKey(uiCmd(QStringLiteral("&Solid"), QString(), QString(), ToolGlyph::Toggle3D,
                  [this]() { app_.setVisualStyle(mm::VisualStyle::Solid); }), QStringLiteral("Alt+4"), false),
        }),
        uiSep(),
        uiKey(uiCmd(QStringLiteral("&Döndürme Merkezi"), QStringLiteral("Dön. Mrk."), QStringLiteral("Döndürme merkezi seç (R)"), ToolGlyph::Toggle3D,
              [this]() { app_.startRotateCenterCommand(); }), QStringLiteral("R"), false),
        uiCmd(QStringLiteral("Döndürme Merkezini &Temizle"), QStringLiteral("Mrk. Sil"), QStringLiteral("Döndürme merkezini temizle"), ToolGlyph::Reset,
              [this]() { app_.clearRotateCenter(); }),
        uiSep(),
        uiCmd(QStringLiteral("&Benchmark (GDI vs GL)"), QStringLiteral("Benchmark"), QStringLiteral("GDI vs GL render benchmark"), ToolGlyph::Extents,
              [this]() {
                  FILE* diag = fopen("model-maker-render.log", "a");
                  if (diag) { fprintf(diag, "MENU-BENCH-CLICKED\n"); fclose(diag); }
                  app_.runRenderBenchmark();
              }),
    }});

    // ---- ÇALIŞMA DÜZLEMİ & UCS + adlandırılmış görünümler ----
    {
        UiEntry ghost = uiKey(uiCmd(QStringLiteral("Çalışma Düzlemi &Hayaleti"), QStringLiteral("Hayalet"), QStringLiteral("Çalışma düzlemi hayaleti (F7)"), ToolGlyph::Toggle3D,
                                    [this]() { app_.toggleWorkPlaneGhost(); }), QStringLiteral("F7"), false);
        ghost.cmd.checked = [this]() { return app_.workPlaneGhostVisible(); };
        cats.push_back({QStringLiteral("Çalışma &Düzlemi"), QStringLiteral("Çalışma Düzlemi"), {
            uiCmd(QStringLiteral("Çalışma &Düzlemi (3 Nokta)"), QStringLiteral("Düzlem"), QStringLiteral("Çalışma düzlemi (3 nokta)"), ToolGlyph::Plane,
                  [this]() { app_.startWorkPlaneCommand(); }),
            uiCmd(QStringLiteral("Düzlemi &Sıfırla (Dünya)"), QStringLiteral("Düz. Sıfır"), QStringLiteral("Düzlemi sıfırla (dünya)"), ToolGlyph::Reset,
                  [this]() { app_.resetWorkPlane(); }),
            ghost,
            uiSep(),
            uiCmd(QStringLiteral("UCS'ye &Dik Görünüş"), QStringLiteral("UCS Dik"), QStringLiteral("UCS'ye dik görünüş"), ToolGlyph::Plane,
                  [this]() { app_.setViewToWorkPlane(); }),
            uiGroup(QStringLiteral("Çalışma Düzlemi (&Global)"), QStringLiteral("Global"), QStringLiteral("Çalışma düzlemi (global)"), ToolGlyph::Plane, {
                uiCmd(QStringLiteral("&XY (Üstten)"), QString(), QString(), ToolGlyph::Plane, [this]() { app_.setGlobalWorkPlane(0); }),
                uiCmd(QStringLiteral("X&Z (Önden)"),  QString(), QString(), ToolGlyph::Plane, [this]() { app_.setGlobalWorkPlane(1); }),
                uiCmd(QStringLiteral("&YZ (Sağdan)"), QString(), QString(), ToolGlyph::Plane, [this]() { app_.setGlobalWorkPlane(2); }),
            }),
            uiSep(),
            uiCmd(QStringLiteral("Görünümü &Adlandır..."), QStringLiteral("Adlandır"), QStringLiteral("Görünümü adlandır..."), ToolGlyph::Save,
                  [this]() { promptSaveNamedView(); }),
            uiCmd(QStringLiteral("Adlandırılmış &Görünüme Git..."), QStringLiteral("Gör. Git"), QStringLiteral("Adlandırılmış görünüme git..."), ToolGlyph::Open,
                  [this]() { promptGotoNamedView(); }),
            uiCmd(QStringLiteral("Adlandırılmış Görünümü S&il..."), QStringLiteral("Gör. Sil"), QStringLiteral("Adlandırılmış görünümü sil..."), ToolGlyph::Delete,
                  [this]() { promptDeleteNamedView(); }),
        }});
    }

    // ---- GÖRÜNÜŞLER: yeni görünüş pencereleri (MDI) ----
    cats.push_back({QStringLiteral("Görünüş&ler"), QStringLiteral("Görünüşler"), {
        uiKey(uiCmd(QStringLiteral("&2 Noktalı Görünüş (XY'ye dik)"), QStringLiteral("2N Gör."), QStringLiteral("2 noktalı görünüş — XY'ye dik (Ctrl+Shift+V)"), ToolGlyph::Plane,
              [this]() { app_.startTwoPointViewCommand(); }), QStringLiteral("Ctrl+Shift+V"), true),
        uiKey(uiCmd(QStringLiteral("&Plan Görünüşü (Z kotu...)"), QStringLiteral("Plan Gör."), QStringLiteral("Plan görünüşü — Z kotu ile (Ctrl+Shift+P)"), ToolGlyph::Extents,
              [this]() { app_.startPlanViewCommand(); }), QStringLiteral("Ctrl+Shift+P"), true),
        uiGroup(QStringLiteral("&Yeni Görünüş"), QStringLiteral("Yeni Görünüş"), QStringLiteral("Tekla tarzı yeni görünüş komutları"), ToolGlyph::Plane, {
            uiCmd(QStringLiteral("&3 Noktalı Görünüş"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startThreePointViewCommand(); }),
            uiCmd(QStringLiteral("&Çalışma Düzleminde Görünüş"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startViewOnWorkPlaneCommand(); }),
            uiSep(),
            uiCmd(QStringLiteral("Parça &Ön Düzlemi"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartPlaneViewCommand(PartPlane::Front); }),
            uiCmd(QStringLiteral("Parça Ü&st Düzlemi"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartPlaneViewCommand(PartPlane::Top); }),
            uiCmd(QStringLiteral("Parça &Uç Düzlemi"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartPlaneViewCommand(PartPlane::End); }),
            uiCmd(QStringLiteral("Parça &Arka Düzlemi"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartPlaneViewCommand(PartPlane::Back); }),
            uiCmd(QStringLiteral("Parça A&lt Düzlemi"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartPlaneViewCommand(PartPlane::Bottom); }),
            uiSep(),
            uiCmd(QStringLiteral("Parçanın &Varsayılan Görünüşleri (Ön/Üst/Uç)"), QString(), QString(), ToolGlyph::Plane,
                  [this]() { app_.startPartDefaultViewsCommand(); }),
            uiCmd(QStringLiteral("Parçanın 3&B Görünüşü"), QString(), QString(), ToolGlyph::Toggle3D,
                  [this]() { app_.startPart3DViewCommand(); }),
            uiSep(),
            uiCmd(QStringLiteral("&Grid Çizgileri Boyunca Görünüşler"), QString(), QString(), ToolGlyph::Extents,
                  [this]() { app_.startGridViewsCommand(); }),
        }),
    }});

    // ---- GRID ----
    cats.push_back({QStringLiteral("&Grid"), QStringLiteral("Grid"), {
        uiCmd(QStringLiteral("&Yapı Gridi Oluştur..."), QStringLiteral("Grid Oluştur"), QStringLiteral("Yapı gridi oluştur..."), ToolGlyph::Extents,
              [this]() { promptCreateGrid(); }),
        uiCmd(QStringLiteral("Yapı &Gridleri..."), QStringLiteral("Gridler"), QStringLiteral("Yapı gridleri..."), ToolGlyph::Extents,
              [this]() { promptGridManager(); }),
    }});

    // ---- PENCERE (yalnız menü) ----
    cats.push_back({QStringLiteral("&Pencere"), QString(), {
        uiCmd(QStringLiteral("&Döşe"), QString(), QString(), ToolGlyph::Extents,
              [this]() { if (mdiArea_) mdiArea_->tileSubWindows(); }),
        uiCmd(QStringLiteral("&Basamakla"), QString(), QString(), ToolGlyph::Extents,
              [this]() { if (mdiArea_) mdiArea_->cascadeSubWindows(); }),
        uiCmd(QStringLiteral("&Model görünüşünü büyüt"), QString(), QString(), ToolGlyph::Extents,
              [this]() {
                  if (modelSubWindow_) { modelSubWindow_->showMaximized(); mdiArea_->setActiveSubWindow(modelSubWindow_); }
              }),
        uiSep(),
        uiCmd(QStringLiteral("Tüm görünüşleri &kapat"), QString(), QString(), ToolGlyph::Delete,
              [this]() { closeAllTwoPointViews(); }),
    }});

    return cats;
}

void QtMainWindow::createMenus() {
    // Tekla tarzi: ust menubar yerine sol ust kosedeki TEK "Menu" dugmesi.
    // Tum menuler bu tek kok menuye (appMenu_) baglanir; dugme createToolbar'da
    // ribbon'un en soluna InstantPopup olarak eklenir. Menubar gizlenir.
    // Icerik TEK KAYNAK commandCategories() tablosundan uretilir (ribbon ile ayni).
    menuBar()->setVisible(false);
    appMenu_ = new QMenu(this);
    appMenu_->setTitle(QStringLiteral("Menü"));

    for (const CommandCategory& cat : commandCategories()) {
        QMenu* menu = appMenu_->addMenu(cat.menuTitle);
        fillUiMenu(menu, this, cat.entries, true);
    }
}

void QtMainWindow::importTeklaBridge() {
    // .mmbridge paket KLASORUNU sectir; import + dogrulama + atomik isleme
    // Application::importTeklaBridge icinde (OCC-on). OCC'siz derlemede uyarir.
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Tekla Component Bridge paket klasörünü seç (.mmbridge)"));
    if (dir.isEmpty()) return;
    app_.importTeklaBridge(std::filesystem::path(dir.toStdWString()));
}

void QtMainWindow::importTeklaBridgeNative() {
    // .mmbridge paket KLASORUNU sectir; native tekla_shear_plate_146 donusumu
    // Application::convertTeklaBridgeToNative icinde (GUID map + secim + gercek
    // OCC + atomik isle). Uye eksikse kullaniciya secim bildirilir.
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Tekla Component Bridge paket klasörünü seç (.mmbridge) — Native dönüşüm"));
    if (dir.isEmpty()) return;
    // Explicit user-supplied NC1 evidence, required for safe real holes.
    const QString nc = QFileDialog::getOpenFileName(
        this, QStringLiteral("Tekla 146 levhasinin DSTV/NC1 dosyasini sec"),
        QString(), QStringLiteral("DSTV NC1 (*.nc1);;Tum dosyalar (*)"));
    if (nc.isEmpty()) return;
    app_.convertTeklaBridgeToNative(std::filesystem::path(dir.toStdWString()),
                                    std::filesystem::path(nc.toStdWString()));
}

void QtMainWindow::saveTeklaBridgeTemplate() {
    // 1) .mmbridge paket KLASORUNU sectir; 2) sablon ADINI sor; 3) Application
    // saveBridgeAsCatalogTemplate (map + derive + save + register). Unsupported
    // geometri icin Application kendi onay diyalogunu acar.
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Tekla Component Bridge paket klasörünü seç (.mmbridge) — Şablon kaynağı"));
    if (dir.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Kataloğa Şablon Kaydet"),
        QStringLiteral("Yeni component şablonunun adı:"), QLineEdit::Normal,
        QStringLiteral("Shear Plate 146 - Özel"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    app_.saveBridgeAsCatalogTemplate(std::filesystem::path(dir.toStdWString()),
                                     name.trimmed().toStdWString());
}

// --- KAYNAK (Weld) komutu + diyalogu (Phase W2) ------------------------------
bool QtMainWindow::openWeldDialog(mm::WeldType& type, double& size, double& length,
                                  mm::WeldClass& cls) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Kaynak (Weld)"));
    auto* form = new QFormLayout(&dlg);

    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItem(QStringLiteral("Fillet (Köşe)"), static_cast<int>(mm::WeldType::Fillet));
    typeCombo->addItem(QStringLiteral("Butt (Alın)"), static_cast<int>(mm::WeldType::Butt));
    typeCombo->setCurrentIndex(type == mm::WeldType::Butt ? 1 : 0);
    form->addRow(QStringLiteral("Tip:"), typeCombo);

    auto* sizeSpin = new QDoubleSpinBox(&dlg);
    sizeSpin->setRange(0.5, 500.0);
    sizeSpin->setDecimals(1);
    sizeSpin->setValue(size <= 0.0 ? 5.0 : size);
    sizeSpin->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("Ölçü (a):"), sizeSpin);

    auto* lenSpin = new QDoubleSpinBox(&dlg);
    lenSpin->setRange(0.0, 100000.0);
    lenSpin->setDecimals(1);
    lenSpin->setValue(length < 0.0 ? 0.0 : length);
    lenSpin->setSuffix(QStringLiteral(" mm"));
    lenSpin->setSpecialValueText(QStringLiteral("Otomatik"));
    form->addRow(QStringLiteral("Uzunluk:"), lenSpin);

    auto* clsCombo = new QComboBox(&dlg);
    clsCombo->addItem(QStringLiteral("Shop (Atölye)"), static_cast<int>(mm::WeldClass::Shop));
    clsCombo->addItem(QStringLiteral("Site (Şantiye)"), static_cast<int>(mm::WeldClass::Site));
    clsCombo->setCurrentIndex(cls == mm::WeldClass::Site ? 1 : 0);
    form->addRow(QStringLiteral("Sınıf:"), clsCombo);

    auto* note = new QLabel(QStringLiteral(
        "Gösterimde gerçek kaynak katısı üretilmez; yalnız konum sembolü çizilir.\n"
        "OK sonrası iki parça seçin: ilk tık ana parça, ikinci tık ikincil parça.\n"
        "Escape komutu iptal eder."), &dlg);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #9FA6B2; font-size: 8pt;"));
    form->addRow(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;
    type = static_cast<mm::WeldType>(typeCombo->currentData().toInt());
    size = sizeSpin->value();
    length = lenSpin->value();
    cls = static_cast<mm::WeldClass>(clsCombo->currentData().toInt());
    return true;
}

void QtMainWindow::startWeldCommand() {
    mm::WeldType type = mm::WeldType::Fillet;
    double size = 5.0, length = 0.0;
    mm::WeldClass cls = mm::WeldClass::Shop;
    if (!openWeldDialog(type, size, length, cls)) return;
    app_.beginWeldCommand(type, size, length, cls);
}

void QtMainWindow::openBoltPropertiesDialog(const mm::BoltGroup& bolt) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Bolt Özellikleri"));
    auto* form = new QFormLayout(&dlg);

    auto* diaCombo = new QComboBox(&dlg);
    for (double d : mm::standardBoltDiameters())
        diaCombo->addItem(QStringLiteral("M%1").arg(d, 0, 'f', 0), d);
    int diaIdx = diaCombo->findData(bolt.params.nominalDiameter);
    if (diaIdx < 0) {
        diaCombo->addItem(QStringLiteral("M%1").arg(bolt.params.nominalDiameter, 0, 'f', 1),
                          bolt.params.nominalDiameter);
        diaIdx = diaCombo->count() - 1;
    }
    diaCombo->setCurrentIndex(diaIdx);
    form->addRow(QStringLiteral("Çap:"), diaCombo);

    auto* gradeCombo = new QComboBox(&dlg);
    gradeCombo->addItem(QStringLiteral("8.8"));
    gradeCombo->addItem(QStringLiteral("10.9"));
    const int gi = gradeCombo->findText(QString::fromStdString(bolt.params.grade));
    if (gi >= 0) gradeCombo->setCurrentIndex(gi);
    form->addRow(QStringLiteral("Kalite:"), gradeCombo);

    auto* holeSpin = new QDoubleSpinBox(&dlg);
    holeSpin->setRange(1.0, 200.0);
    holeSpin->setDecimals(1);
    holeSpin->setValue(bolt.params.holeDiameter);
    form->addRow(QStringLiteral("Delik çapı (mm):"), holeSpin);

    auto* headChk = new QCheckBox(QStringLiteral("Baş"), &dlg);
    headChk->setChecked(bolt.params.hasHead);
    auto* nutChk = new QCheckBox(QStringLiteral("Somun"), &dlg);
    nutChk->setChecked(bolt.params.hasNut);
    auto* washChk = new QCheckBox(QStringLiteral("Pul"), &dlg);
    washChk->setChecked(bolt.params.hasWasher);
    auto* accRow = new QHBoxLayout;
    accRow->addWidget(headChk);
    accRow->addWidget(nutChk);
    accRow->addWidget(washChk);
    accRow->addStretch(1);
    form->addRow(QStringLiteral("Elemanlar:"), accRow);

    auto* countXSpin = new QSpinBox(&dlg);
    countXSpin->setRange(1, 200);
    countXSpin->setValue(bolt.countX > 0 ? bolt.countX : 1);
    form->addRow(QStringLiteral("Adet X:"), countXSpin);

    auto* countYSpin = new QSpinBox(&dlg);
    countYSpin->setRange(1, 200);
    countYSpin->setValue(bolt.countY > 0 ? bolt.countY : 1);
    form->addRow(QStringLiteral("Adet Y:"), countYSpin);

    auto* spacingXSpin = new QDoubleSpinBox(&dlg);
    spacingXSpin->setRange(-100000.0, 100000.0);
    spacingXSpin->setDecimals(1);
    spacingXSpin->setValue(bolt.spacingX);
    form->addRow(QStringLiteral("Mesafe X (mm):"), spacingXSpin);

    auto* spacingYSpin = new QDoubleSpinBox(&dlg);
    spacingYSpin->setRange(-100000.0, 100000.0);
    spacingYSpin->setDecimals(1);
    spacingYSpin->setValue(bolt.spacingY);
    form->addRow(QStringLiteral("Mesafe Y (mm):"), spacingYSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    mm::BoltGroup updated = bolt;
    updated.params.nominalDiameter = diaCombo->currentData().toDouble();
    updated.params.holeDiameter = holeSpin->value();
    updated.params.grade = gradeCombo->currentText().toStdString();
    updated.params.hasHead = headChk->isChecked();
    updated.params.hasNut = nutChk->isChecked();
    updated.params.hasWasher = washChk->isChecked();
    updated.countX = countXSpin->value();
    updated.countY = countYSpin->value();
    updated.spacingX = spacingXSpin->value();
    updated.spacingY = spacingYSpin->value();
    app_.setSelectedBoltGroup(updated);
}

bool QtMainWindow::openEndPlateParamsDialog(const mm::EndPlateParams& current,
                                            mm::EndPlateParams& out) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("End Plate (Alın Levhası) Özellikleri"));
    dlg.setMinimumWidth(360);

    auto* root = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget(&dlg);

    // ── Resim (Picture) sekmesi: konum + boşluk ────────────────────────────
    auto* picPage = new QWidget(&dlg);
    auto* picForm = new QFormLayout(picPage);

    auto* extraSpin = new QDoubleSpinBox(picPage);
    extraSpin->setRange(0.0, 300.0);
    extraSpin->setDecimals(1);
    extraSpin->setValue(current.plateExtra);
    extraSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Yatay pay (levha genişliği = kesit + 2×pay):"), extraSpin);

    const double effUpper = (current.upperEdgeDistance > -1e8)
                                ? current.upperEdgeDistance
                                : -((current.topExtra >= 0.0) ? current.topExtra : current.plateExtra);
    const double effLower = (current.lowerEdgeDistance > -1e8)
                                ? current.lowerEdgeDistance
                                : -((current.bottomExtra >= 0.0) ? current.bottomExtra : current.plateExtra);

    auto* upperEdgeSpin = new QDoubleSpinBox(picPage);
    upperEdgeSpin->setRange(-300.0, 300.0);
    upperEdgeSpin->setDecimals(1);
    upperEdgeSpin->setValue(effUpper);
    upperEdgeSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Üst kenar mesafesi (kesit üstü → levha üstü):"), upperEdgeSpin);

    auto* lowerEdgeSpin = new QDoubleSpinBox(picPage);
    lowerEdgeSpin->setRange(-300.0, 300.0);
    lowerEdgeSpin->setDecimals(1);
    lowerEdgeSpin->setValue(effLower);
    lowerEdgeSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Alt kenar mesafesi (kesit altı → levha altı):"), lowerEdgeSpin);

    auto* gapSpin = new QDoubleSpinBox(picPage);
    gapSpin->setRange(0.0, 100.0);
    gapSpin->setDecimals(1);
    gapSpin->setValue(current.gap);
    gapSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Kolon boşluğu (levha ↔ ana parça):"), gapSpin);

    auto* wOvrSpin = new QDoubleSpinBox(picPage);
    wOvrSpin->setRange(0.0, 3000.0);
    wOvrSpin->setDecimals(1);
    wOvrSpin->setValue(current.plateWidthOverride);
    wOvrSpin->setSpecialValueText(QStringLiteral("Otomatik"));
    wOvrSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Levha genişliği (0 = otomatik):"), wOvrSpin);

    auto* hOvrSpin = new QDoubleSpinBox(picPage);
    hOvrSpin->setRange(0.0, 3000.0);
    hOvrSpin->setDecimals(1);
    hOvrSpin->setValue(current.plateHeightOverride);
    hOvrSpin->setSpecialValueText(QStringLiteral("Otomatik"));
    hOvrSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Levha yüksekliği (0 = otomatik):"), hOvrSpin);

    auto* picNote = new QLabel(picPage);
    picNote->setWordWrap(true);
    picNote->setStyleSheet(QStringLiteral("color: gray;"));
    picNote->setText(QStringLiteral(
        "Tekla 144 Resim sekmesi: pozitif kenar mesafesi levhayı küçültür, negatif "
        "büyütür (levha kirişin gerçek kesitine ortalanır). Genişlik/yükseklik 0 ise "
        "kesitten otomatik türetilir."));
    picForm->addRow(picNote);

    // ── Levha (Plates) sekmesi: kalınlık ───────────────────────────────────
    auto* pltPage = new QWidget(&dlg);
    auto* pltForm = new QFormLayout(pltPage);

    auto* thickSpin = new QDoubleSpinBox(pltPage);
    thickSpin->setRange(1.0, 200.0);
    thickSpin->setDecimals(1);
    thickSpin->setValue(current.plateThickness);
    thickSpin->setSuffix(QStringLiteral(" mm"));
    pltForm->addRow(QStringLiteral("Levha kalınlığı:"), thickSpin);

    auto* matEdit = new QLineEdit(pltPage);
    matEdit->setText(QString::fromStdString(current.plateMaterial));
    matEdit->setPlaceholderText(QStringLiteral("örn. S235JR"));
    pltForm->addRow(QStringLiteral("Levha malzemesi:"), matEdit);

    auto* nameEdit = new QLineEdit(pltPage);
    nameEdit->setText(QString::fromStdString(current.plateName));
    nameEdit->setPlaceholderText(QStringLiteral("örn. ENDPLATE"));
    pltForm->addRow(QStringLiteral("Levha adı:"), nameEdit);

    auto* finEdit = new QLineEdit(pltPage);
    finEdit->setText(QString::fromStdString(current.plateFinish));
    finEdit->setPlaceholderText(QStringLiteral("örn. HDG, Paint"));
    pltForm->addRow(QStringLiteral("Yüzey işlemi:"), finEdit);

    auto* pltNote = new QLabel(pltPage);
    pltNote->setWordWrap(true);
    pltNote->setStyleSheet(QStringLiteral("color: gray;"));
    pltNote->setText(QStringLiteral(
        "Levha genişliği ve yüksekliği, bulon grubu kenar payları ve kesit "
        "boyutundan otomatik türetilir (Tekla 144 Plates sekmesi karşılığı)."));
    pltForm->addRow(pltNote);

    // ── Bulonlar (Bolts) sekmesi ──────────────────────────────────────────
    auto* bltPage = new QWidget(&dlg);
    auto* bltForm = new QFormLayout(bltPage);

    auto* diaCombo = new QComboBox(bltPage);
    for (double d : mm::standardBoltDiameters())
        diaCombo->addItem(QStringLiteral("M%1").arg(d, 0, 'f', 0), d);
    int diaIdx = diaCombo->findData(current.boltDiameter);
    if (diaIdx < 0) {
        diaCombo->addItem(QStringLiteral("M%1").arg(current.boltDiameter, 0, 'f', 1),
                          current.boltDiameter);
        diaIdx = diaCombo->count() - 1;
    }
    diaCombo->setCurrentIndex(diaIdx);
    bltForm->addRow(QStringLiteral("Bulon çapı:"), diaCombo);

    auto* gradeCombo = new QComboBox(bltPage);
    gradeCombo->addItem(QStringLiteral("8.8"));
    gradeCombo->addItem(QStringLiteral("10.9"));
    const int gi = gradeCombo->findText(QString::fromStdString(current.boltGrade));
    if (gi >= 0) gradeCombo->setCurrentIndex(gi);
    bltForm->addRow(QStringLiteral("Bulon kalitesi:"), gradeCombo);

    auto* holeSpin = new QDoubleSpinBox(bltPage);
    holeSpin->setRange(1.0, 200.0);
    holeSpin->setDecimals(1);
    holeSpin->setValue(current.holeDiameter);
    holeSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Delik çapı:"), holeSpin);

    auto* countXSpin = new QSpinBox(bltPage);
    countXSpin->setRange(1, 50);
    countXSpin->setValue(current.boltCountX > 0 ? current.boltCountX : 1);
    bltForm->addRow(QStringLiteral("Bulon adedi (yatay, X):"), countXSpin);

    auto* countYSpin = new QSpinBox(bltPage);
    countYSpin->setRange(1, 50);
    countYSpin->setValue(current.boltCountY > 0 ? current.boltCountY : 1);
    bltForm->addRow(QStringLiteral("Bulon adedi (dikey, Y):"), countYSpin);

    auto* edgeSpin = new QDoubleSpinBox(bltPage);
    edgeSpin->setRange(1.0, 500.0);
    edgeSpin->setDecimals(1);
    edgeSpin->setValue(current.boltEdge);
    edgeSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Kenar payı (bulon merkezi → levha kenarı):"), edgeSpin);

    // ── Bulon grubu konumu (Tekla Bolt group dimensions) ──────────────────
    auto* alignHCombo = new QComboBox(bltPage);
    alignHCombo->addItem(QStringLiteral("Sol"), 0);
    alignHCombo->addItem(QStringLiteral("Orta"), 1);
    alignHCombo->addItem(QStringLiteral("Sağ"), 2);
    int ah = (current.boltAlignH >= 0 && current.boltAlignH <= 2) ? current.boltAlignH : 1;
    alignHCombo->setCurrentIndex(ah);
    bltForm->addRow(QStringLiteral("Yatay ölçüm (bulon grubu):"), alignHCombo);

    auto* offHSpin = new QDoubleSpinBox(bltPage);
    offHSpin->setRange(-500.0, 500.0);
    offHSpin->setDecimals(1);
    offHSpin->setValue(current.boltOffsetH);
    offHSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Yatay konum ölçüsü:"), offHSpin);

    auto* alignVCombo = new QComboBox(bltPage);
    alignVCombo->addItem(QStringLiteral("Üst"), 0);
    alignVCombo->addItem(QStringLiteral("Orta"), 1);
    alignVCombo->addItem(QStringLiteral("Alt"), 2);
    int av = (current.boltAlignV >= 0 && current.boltAlignV <= 2) ? current.boltAlignV : 2;
    alignVCombo->setCurrentIndex(av);
    bltForm->addRow(QStringLiteral("Dikey ölçüm (bulon grubu):"), alignVCombo);

    auto* offVSpin = new QDoubleSpinBox(bltPage);
    offVSpin->setRange(-500.0, 500.0);
    offVSpin->setDecimals(1);
    offVSpin->setValue(current.boltOffsetV);
    offVSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Dikey konum ölçüsü:"), offVSpin);

    auto* spacingXSpin = new QDoubleSpinBox(bltPage);
    spacingXSpin->setRange(0.0, 2000.0);
    spacingXSpin->setDecimals(1);
    spacingXSpin->setValue(current.boltSpacingX);
    spacingXSpin->setSuffix(QStringLiteral(" mm"));
    spacingXSpin->setSpecialValueText(QStringLiteral("Otomatik"));
    bltForm->addRow(QStringLiteral("Yatay aralık (0 = otomatik):"), spacingXSpin);

    auto* spacingYSpin = new QDoubleSpinBox(bltPage);
    spacingYSpin->setRange(0.0, 2000.0);
    spacingYSpin->setDecimals(1);
    spacingYSpin->setValue(current.boltSpacingY);
    spacingYSpin->setSuffix(QStringLiteral(" mm"));
    spacingYSpin->setSpecialValueText(QStringLiteral("Otomatik"));
    bltForm->addRow(QStringLiteral("Dikey aralık (0 = otomatik):"), spacingYSpin);

    // Değişken bulon aralıkları (Tekla "each space between bolts"): N bulon için
    // N-1 ayrı değer, boşlukla ayrılmış. Boş = sabit/otomatik aralık kullanılır.
    QString varSxText;
    for (std::size_t k = 0; k < current.boltSpacingXs.size(); ++k) {
        if (k) varSxText += QLatin1Char(' ');
        varSxText += QString::number(current.boltSpacingXs[k]);
    }
    auto* varSpXEdit = new QLineEdit(bltPage);
    varSpXEdit->setText(varSxText);
    varSpXEdit->setPlaceholderText(QStringLiteral("boş = otomatik; N-1 değer, boşlukla ayrılmış"));
    bltForm->addRow(QStringLiteral("Yatay aralıklar (N-1):"), varSpXEdit);

    QString varSyText;
    for (std::size_t k = 0; k < current.boltSpacingYs.size(); ++k) {
        if (k) varSyText += QLatin1Char(' ');
        varSyText += QString::number(current.boltSpacingYs[k]);
    }
    auto* varSpYEdit = new QLineEdit(bltPage);
    varSpYEdit->setText(varSyText);
    varSpYEdit->setPlaceholderText(QStringLiteral("boş = otomatik; N-1 değer, boşlukla ayrılmış"));
    bltForm->addRow(QStringLiteral("Dikey aralıklar (N-1):"), varSpYEdit);

    QString delText;
    for (std::size_t k = 0; k < current.deletedBolts.size(); ++k) {
        if (k) delText += QLatin1Char(',');
        delText += QString::number(current.deletedBolts[k]);
    }
    auto* delEdit = new QLineEdit(bltPage);
    delEdit->setText(delText);
    delEdit->setPlaceholderText(QStringLiteral("örn. 2,4 (numaralar 1'den başlar)"));
    bltForm->addRow(QStringLiteral("Silinecek bulonlar:"), delEdit);

    auto* headChk = new QCheckBox(QStringLiteral("Baş"), bltPage);
    headChk->setChecked(current.hasHead);
    auto* nutChk = new QCheckBox(QStringLiteral("Somun"), bltPage);
    nutChk->setChecked(current.hasNut);
    auto* washChk = new QCheckBox(QStringLiteral("Pul"), bltPage);
    washChk->setChecked(current.hasWasher);
    auto* accRow = new QHBoxLayout;
    accRow->addWidget(headChk);
    accRow->addWidget(nutChk);
    accRow->addWidget(washChk);
    accRow->addStretch(1);
    bltForm->addRow(QStringLiteral("Bulon elemanları:"), accRow);

    // ── Bulon temel özellikleri (Tekla Bolt basic properties) ─────────────
    auto* stdCombo = new QComboBox(bltPage);
    stdCombo->setEditable(true);
    stdCombo->addItem(QStringLiteral("ISO 4014 (altıgen baş, kısmi diş)"));
    stdCombo->addItem(QStringLiteral("ISO 4017 (altıgen baş, tam diş)"));
    stdCombo->setCurrentText(QString::fromStdString(current.boltStandard));
    bltForm->addRow(QStringLiteral("Bulon standardı:"), stdCombo);

    // Tolerans = delik çapı − bulon çapı (Tekla Tolerance). Delik çapına yazar.
    auto* tolSpin = new QDoubleSpinBox(bltPage);
    tolSpin->setRange(0.0, 20.0);
    tolSpin->setDecimals(1);
    tolSpin->setValue(current.holeDiameter - current.boltDiameter);
    tolSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Tolerans (delik − bulon):"), tolSpin);
    QObject::connect(tolSpin, &QDoubleSpinBox::textChanged,
                     [diaCombo, holeSpin, tolSpin](const QString&) {
                         holeSpin->setValue(diaCombo->currentData().toDouble() +
                                            tolSpin->value());
                     });

    auto* threadChk = new QCheckBox(QStringLiteral("Diş bulonlu parçada"), bltPage);
    threadChk->setChecked(current.threadInMat);
    bltForm->addRow(QStringLiteral("Thread in mat:"), threadChk);

    auto* siteCombo = new QComboBox(bltPage);
    siteCombo->addItem(QStringLiteral("Site"), 0);
    siteCombo->addItem(QStringLiteral("Workshop"), 1);
    siteCombo->setCurrentIndex(current.siteWorkshop == 1 ? 1 : 0);
    bltForm->addRow(QStringLiteral("Site / Workshop:"), siteCombo);

    auto* cutCombo = new QComboBox(bltPage);
    cutCombo->addItem(QStringLiteral("Tek flanş"), 0);
    cutCombo->addItem(QStringLiteral("Çift flanş (kolon boyunca)"), 1);
    cutCombo->setCurrentIndex(current.boltCutLengthMode == 1 ? 1 : 0);
    bltForm->addRow(QStringLiteral("Kesim derinliği (cut length):"), cutCombo);

    auto* lenIncSpin = new QDoubleSpinBox(bltPage);
    lenIncSpin->setRange(0.0, 200.0);
    lenIncSpin->setDecimals(1);
    lenIncSpin->setValue(current.boltLengthIncrease);
    lenIncSpin->setSuffix(QStringLiteral(" mm"));
    bltForm->addRow(QStringLiteral("Bulon boy artışı:"), lenIncSpin);

    auto* dirCombo = new QComboBox(bltPage);
    dirCombo->addItem(QStringLiteral("1 — baş kolonda"), 1);
    dirCombo->addItem(QStringLiteral("2 — baş kirişte"), 2);
    dirCombo->setCurrentIndex(current.boltDirection == 2 ? 1 : 0);
    bltForm->addRow(QStringLiteral("Bulon yönü (bolting direction):"), dirCombo);

    auto* commentEdit = new QLineEdit(bltPage);
    commentEdit->setText(QString::fromStdString(current.boltComment));
    bltForm->addRow(QStringLiteral("Yorum:"), commentEdit);

    tabs->addTab(picPage, QStringLiteral("Resim"));
    tabs->addTab(pltPage, QStringLiteral("Levha"));
    tabs->addTab(bltPage, QStringLiteral("Bulonlar"));
    root->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;

    out = current;
    out.plateExtra = extraSpin->value();
    out.upperEdgeDistance = upperEdgeSpin->value();
    out.lowerEdgeDistance = lowerEdgeSpin->value();
    out.plateWidthOverride = wOvrSpin->value();
    out.plateHeightOverride = hOvrSpin->value();
    out.gap = gapSpin->value();
    out.plateThickness = thickSpin->value();
    out.plateMaterial = matEdit->text().trimmed().toStdString();
    out.plateName = nameEdit->text().trimmed().toStdString();
    out.plateFinish = finEdit->text().trimmed().toStdString();
    out.boltDiameter = diaCombo->currentData().toDouble();
    out.boltGrade = gradeCombo->currentText().toStdString();
    out.holeDiameter = holeSpin->value();
    out.boltCountX = countXSpin->value();
    out.boltCountY = countYSpin->value();
    out.boltEdge = edgeSpin->value();
    out.boltAlignH = alignHCombo->currentData().toInt();
    out.boltAlignV = alignVCombo->currentData().toInt();
    out.boltOffsetH = offHSpin->value();
    out.boltOffsetV = offVSpin->value();
    out.boltSpacingX = spacingXSpin->value();
    out.boltSpacingY = spacingYSpin->value();
    out.boltSpacingXs.clear();
    for (const auto& t : varSpXEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = t.trimmed().toDouble(&ok);
        if (ok) out.boltSpacingXs.push_back(v);
    }
    out.boltSpacingYs.clear();
    for (const auto& t : varSpYEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = t.trimmed().toDouble(&ok);
        if (ok) out.boltSpacingYs.push_back(v);
    }
    out.deletedBolts.clear();
    for (const auto& t : delEdit->text().split(QLatin1Char(','))) {
        bool ok = false;
        const int n = t.trimmed().toInt(&ok);
        if (ok && n >= 1) out.deletedBolts.push_back(n);
    }
    out.hasHead = headChk->isChecked();
    out.hasNut = nutChk->isChecked();
    out.hasWasher = washChk->isChecked();
    out.boltStandard = stdCombo->currentText().toStdString();
    out.threadInMat = threadChk->isChecked();
    out.siteWorkshop = siteCombo->currentData().toInt();
    out.boltCutLengthMode = cutCombo->currentData().toInt();
    out.boltLengthIncrease = lenIncSpin->value();
    out.boltDirection = dirCombo->currentData().toInt();
    out.boltComment = commentEdit->text().toStdString();
    return true;
}

// UNIVERSAL PARAMETRIK COMPONENT parametre diyalogu (Fin Plate vb.).
// Parametre semasi registry'den gelir (tip-agnostik): yeni bir component tipi
// eklemek icin burada degisiklik GEREKMEZ. OK -> setSelectedComponentParams,
// yani mevcut ComponentMaterializer::recompute yolu (undo'lu, tek kayit).
bool QtMainWindow::openComponentParamsDialog(const mm::ComponentInstanceRecord& rec) {
    const mm::ComponentType* type = mm::ComponentRegistry::instance().findType(rec.typeId);
    if (!type) return false;

    // Şematik parametre editörü: registry'de diyalog tanımı varsa (fin_plate vb.)
    // Tekla-tarzi sol çizim + sağ gruplanmış editörler + çift yönlü hotspot vurgusu.
    if (const auto* def = mm::ComponentDialogRegistry::instance().find(rec.typeId)) {
        // TEKLA (sekmeli) pencere MODELESS: pencere acikken modelde baska baglantilar
        // secilebilir. Apply/Modify/Get/Create calisir:
        //   Apply  : pencere degerlerini duzenlenen baglantiya uygular (acik kalir)
        //   Modify : degerleri SECILI tum ayni tip baglantilara uygular
        //   Get    : secili baglantinin degerlerini pencereye alir
        //   Create : degerleri yeni baglanti icin saklar, yerlestirme komutunu baslatir
        //   OK     : duzenlenen baglantiya uygular ve kapatir
        if (def->schema.useTabs) {
            struct ModelessHandle {
                QPointer<QDialog> dlg;
                std::function<void(const mm::ComponentInstanceRecord&)> load;
            };
            static std::map<std::wstring, ModelessHandle> openDialogs;
            if (auto it = openDialogs.find(rec.typeId);
                it != openDialogs.end() && !it->second.dlg.isNull()) {
                // Ayni tipte pencere zaten acik: tekrar acma, bu baglantiyi yukle.
                it->second.load(rec);
                it->second.dlg->show();
                it->second.dlg->raise();
                it->second.dlg->activateWindow();
                return true;
            }
            auto* dlg = new QDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setModal(false);
            dlg->setWindowFlag(Qt::Tool, true);  // ana pencerenin uzerinde kalir
            auto* root = new QVBoxLayout(dlg);
            auto* widget = new mm::ComponentDialogWidget(*def, rec.params, dlg);
            widget->setPresetStore(&componentPresetStore(), persistComponentPresets);
            if(rec.typeId==L"tekla_haunch_40") {
                if(auto source=app_.componentPreviewInput(rec.id))
                    widget->setPreviewInput(*source);
                dlg->resize(1140,720);
            }
            root->addWidget(widget);
            const QString baseTitle = QString::fromStdWString(type->displayName);
            auto curId = std::make_shared<mm::EntityId>(rec.id);
            const std::wstring typeId = rec.typeId;
            const auto retitle = [dlg, baseTitle, curId]() {
                dlg->setWindowTitle(baseTitle + QStringLiteral(" — #") +
                                    QString::number(static_cast<qulonglong>(*curId)));
            };
            retitle();

            // Pencere degerleri + kaynak tablosu ekstralari (createWelds/weldSize).
            const auto collect = [widget]() {
                auto params = widget->resultParams();
                bool anyWeld = false;
                double weldSize = 5.0;
                for (const auto& r : widget->resultWeldRows())
                    if (r.enabled) { anyWeld = true; weldSize = r.size; break; }
                params.push_back(mm::CompParam{L"createWelds", anyWeld});
                params.push_back(mm::CompParam{L"weldSize", weldSize});
                return params;
            };
            const auto say = [this](const QString& m) { statusBar()->showMessage(m, 5000); };

            mm::ComponentDialogWidget::ButtonActions act;
            act.apply = [this, curId, collect, say]() {
                say(app_.setSelectedComponentParams(*curId, collect())
                        ? QStringLiteral("Uygulandı")
                        : QStringLiteral("Uygulanamadı (geometri üretilemedi)"));
            };
            act.modify = [this, typeId, collect, say]() {
                const auto targets = app_.selectedComponentInstances(typeId);
                if (targets.empty()) {
                    say(QStringLiteral("Modify: modelde aynı tipte seçili bağlantı yok"));
                    return;
                }
                const auto params = collect();
                int ok = 0;
                for (const auto& t : targets)
                    if (app_.setSelectedComponentParams(t.id, params)) ++ok;
                say(QStringLiteral("Modify: %1/%2 bağlantı güncellendi")
                        .arg(ok).arg(static_cast<int>(targets.size())));
            };
            act.get = [this, typeId, widget, curId, retitle, say]() {
                const auto targets = app_.selectedComponentInstances(typeId);
                if (targets.empty()) {
                    say(QStringLiteral("Get: modelde aynı tipte seçili bağlantı yok"));
                    return;
                }
                widget->setParams(targets.front().params);
                if(targets.front().typeId==L"tekla_haunch_40") {
                    if(auto source=app_.componentPreviewInput(targets.front().id))
                        widget->setPreviewInput(*source);
                }
                *curId = targets.front().id;
                retitle();
                say(QStringLiteral("Get: bağlantı #%1 değerleri alındı")
                        .arg(static_cast<qulonglong>(targets.front().id)));
            };
            act.create = [this, typeId, collect]() {
                // Yeni baglanti: pencere ACIK kalir; kullanici ana + ikincil parcayi secer.
                app_.setPendingComponentParams(collect());
                app_.startComponentCommand(typeId);
            };
            root->addWidget(widget->createButtonBar(dlg, act));
            // OK = duzenlenen baglantiya uygula + kapat; Cancel = kapat (bar bagladi).
            QObject::connect(dlg, &QDialog::accepted, this, [this, curId, collect]() {
                app_.setSelectedComponentParams(*curId, collect());
            });
            // Disaridan (cift tik) gelen baglantiyi pencereye yukle.
            openDialogs[typeId] = ModelessHandle{
                QPointer<QDialog>(dlg),
                [this, widget, curId, retitle](const mm::ComponentInstanceRecord& r) {
                    widget->setParams(r.params);
                    if(r.typeId==L"tekla_haunch_40") {
                        if(auto source=app_.componentPreviewInput(r.id))
                            widget->setPreviewInput(*source);
                    }
                    *curId = r.id;
                    retitle();
                }};
            dlg->resize(dlg->sizeHint().expandedTo(QSize(760, 560)));
            dlg->show();
            return true;
        }

        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Bileşen Özellikleri — ") +
                           QString::fromStdWString(type->displayName));
        dlg.setMinimumSize(760, 560);
        auto* root = new QVBoxLayout(&dlg);
        auto* widget = new mm::ComponentDialogWidget(*def, rec.params, &dlg);
        widget->setPresetStore(&componentPresetStore(), persistComponentPresets);
        root->addWidget(widget);
        if (def->schema.useTabs) {
            // Tekla duzeni: OK Apply Modify Get [x/o] Cancel .... Create
            root->addWidget(widget->createButtonBar(&dlg));
            dlg.setWindowTitle(QString::fromStdWString(type->displayName));
            // Tekla sekmeleri sabit boyutlu (ekran goruntusu): diyalog icerige gore.
            dlg.resize(dlg.sizeHint().expandedTo(QSize(760, 560)));
        } else {
            auto* buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            root->addWidget(buttons);
        }
        if (dlg.exec() != QDialog::Accepted) return false;
        auto params = widget->resultParams();
        // Kaynak tablosu -> generator: en az bir satir aciksa createWelds=true,
        // ilk acik satirin olcusu weldSize olur. Bunlar semada YOK (ekstra
        // parametre) — alignParamsToSpecs bilinmeyen parametreleri KORUR, generator
        // paramAsBool/paramAsDouble ile okur. Boylece "diyalogda kaynaklar acik"
        // durumu baglantinin gercek kaynak uretmesine dogrudan baglanir.
        const auto weldRows = widget->resultWeldRows();
        bool anyWeld = false;
        double weldSize = 5.0;
        for (const auto& r : weldRows)
            if (r.enabled) { anyWeld = true; weldSize = r.size; break; }
        params.push_back(mm::CompParam{L"createWelds", anyWeld});
        params.push_back(mm::CompParam{L"weldSize", weldSize});
        return app_.setSelectedComponentParams(rec.id, params);
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Bileşen Özellikleri — ") +
                       QString::fromStdWString(type->displayName));
    dlg.setMinimumWidth(420);

    auto* root = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);

    const std::vector<mm::CompParam> aligned =
        mm::alignParamsToSpecs(type->params, rec.params);

    // Her spec icin bir editor; indeksler spec indeksiyle hizali.
    std::vector<QWidget*> editors(type->params.size(), nullptr);
    for (std::size_t i = 0; i < type->params.size(); ++i) {
        const mm::CompParamSpec& spec = type->params[i];
        const mm::CompParam* stored = mm::findParam(aligned, spec.name);
        const mm::CompParamValue value = stored ? stored->value : mm::paramDefaultValue(spec);
        const QString label = QString::fromStdWString(
            spec.description.empty() ? spec.name : spec.description);

        QWidget* editor = nullptr;
        switch (spec.type) {
            case mm::CompParamType::Length:
            case mm::CompParamType::Angle: {
                auto* spin = new QDoubleSpinBox(&dlg);
                spin->setDecimals(2);
                spin->setRange(spec.hasMinDouble ? spec.minDouble : -1.0e6,
                               spec.hasMaxDouble ? spec.maxDouble : 1.0e6);
                double v = spec.defDouble;
                if (const auto* d = std::get_if<double>(&value)) v = *d;
                spin->setValue(v);
                if (!spec.unit.empty())
                    spin->setSuffix(QStringLiteral(" ") +
                                    QString::fromStdWString(spec.unit));
                editor = spin;
                break;
            }
            case mm::CompParamType::Integer: {
                auto* spin = new QSpinBox(&dlg);
                spin->setRange(spec.hasMinInt ? spec.minInt : -1000000,
                               spec.hasMaxInt ? spec.maxInt : 1000000);
                int v = spec.defInt;
                if (const auto* n = std::get_if<int>(&value)) v = *n;
                spin->setValue(v);
                editor = spin;
                break;
            }
            case mm::CompParamType::Boolean: {
                auto* box = new QCheckBox(&dlg);
                bool v = spec.defBool;
                if (const auto* b = std::get_if<bool>(&value)) v = *b;
                box->setChecked(v);
                editor = box;
                break;
            }
            case mm::CompParamType::Enum: {
                auto* combo = new QComboBox(&dlg);
                for (const auto& choice : spec.choices)
                    combo->addItem(QString::fromStdWString(choice));
                std::wstring v = spec.defString;
                if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
                const int idx = combo->findText(QString::fromStdWString(v));
                if (idx >= 0) combo->setCurrentIndex(idx);
                editor = combo;
                break;
            }
            case mm::CompParamType::String:
            case mm::CompParamType::Profile:
            case mm::CompParamType::Material: {
                auto* edit = new QLineEdit(&dlg);
                std::wstring v = spec.defString;
                if (const auto* s = std::get_if<std::wstring>(&value)) v = *s;
                edit->setText(QString::fromStdWString(v));
                editor = edit;
                break;
            }
            case mm::CompParamType::EntityRef: {
                // Varlik referanslari bu diyalogdan duzenlenmez (model secimi ister);
                // mevcut deger salt-okunur gosterilir.
                auto* edit = new QLineEdit(&dlg);
                edit->setReadOnly(true);
                unsigned long long v = 0;
                if (const auto* e = std::get_if<mm::EntityId>(&value)) v = *e;
                edit->setText(QString::number(v));
                editor = edit;
                break;
            }
        }
        editors[i] = editor;
        if (editor) form->addRow(label, editor);
    }

    if (!type->params.empty()) {
        auto* note = new QLabel(&dlg);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray;"));
        note->setText(QStringLiteral(
            "Değişiklikler OK ile uygulanır ve bağlantı yeniden hesaplanır "
            "(levha, bulon ve delikler güncellenir). Tek undo adımı oluşur."));
        root->addWidget(note);
    }

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addLayout(form);
    root->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    std::vector<mm::CompParam> params;
    params.reserve(type->params.size());
    for (std::size_t i = 0; i < type->params.size(); ++i) {
        const mm::CompParamSpec& spec = type->params[i];
        QWidget* editor = editors[i];
        if (!editor || spec.type == mm::CompParamType::EntityRef) continue;
        mm::CompParamValue value{spec.defDouble};
        switch (spec.type) {
            case mm::CompParamType::Length:
            case mm::CompParamType::Angle:
                value = static_cast<QDoubleSpinBox*>(editor)->value();
                break;
            case mm::CompParamType::Integer:
                value = static_cast<QSpinBox*>(editor)->value();
                break;
            case mm::CompParamType::Boolean:
                value = static_cast<QCheckBox*>(editor)->isChecked();
                break;
            case mm::CompParamType::Enum:
                value = static_cast<QComboBox*>(editor)->currentText().toStdWString();
                break;
            default:
                value = static_cast<QLineEdit*>(editor)->text().toStdWString();
                break;
        }
        params.push_back(mm::CompParam{spec.name, value});
    }
    return app_.setSelectedComponentParams(rec.id, params);
}

void QtMainWindow::promptSaveNamedView() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Görünümü Adlandır"),
        QStringLiteral("Görünüm adı:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    const std::wstring wname = name.trimmed().toStdWString();
    if (wname.empty()) return;
    if (!app_.saveNamedView(wname)) {
        QMessageBox::warning(this, QStringLiteral("Hata"),
            QStringLiteral("Görünüm kaydedilemedi."));
        return;
    }
    app_.publishStatus(L"Görünüm kaydedildi: " + wname);
}

void QtMainWindow::promptGotoNamedView() {
    const auto names = app_.namedViewNames();
    if (names.empty()) {
        QMessageBox::information(this, QStringLiteral("Adlandırılmış Görünümler"),
            QStringLiteral("Kayıtlı görünüm yok."));
        return;
    }
    QStringList items;
    for (const auto& n : names) items << QString::fromStdWString(n);
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Görünüme Git"),
        QStringLiteral("Görünüm:"), items, 0, false, &ok);
    if (ok && !pick.isEmpty()) app_.applyNamedView(pick.toStdWString());
}

void QtMainWindow::promptDeleteNamedView() {
    const auto names = app_.namedViewNames();
    if (names.empty()) {
        QMessageBox::information(this, QStringLiteral("Adlandırılmış Görünümler"),
            QStringLiteral("Kayıtlı görünüm yok."));
        return;
    }
    QStringList items;
    for (const auto& n : names) items << QString::fromStdWString(n);
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Görünümü Sil"),
        QStringLiteral("Görünüm:"), items, 0, false, &ok);
    if (ok && !pick.isEmpty()) app_.removeNamedView(pick.toStdWString());
}

void QtMainWindow::promptCreateGrid() {
    // TEKLA "Create rectangular grid": grid SPESIFIKASYONU ile olusturulur.
    // Tekla varsayilani gibi 4x4 aks + 3 kot ile baslar; property pane'de
    // degistirilip onaylanir.
    GridDefinition grid;
    grid.name = "GRID";
    grid.xCoords = parseGridCoordinateLine("0 3*6000", true);
    grid.yCoords = parseGridCoordinateLine("0 3*5000", true);
    grid.zCoords = parseGridCoordinateLine("0 3600 7200", false);
    // X ailesi HARF DUZ SIRA (en soldaki/orijin aksi = A, saga dogru artar),
    // Y ailesi SAYI (asagidan yukari). Kullanici sarti.
    grid.xLabels.clear();
    for (std::size_t i = 0; i < grid.xCoords.size(); ++i)
        grid.xLabels.push_back(gridAutoLetterLabel(i));
    grid.yLabels = parseGridLabelLine("1 2 3 4");
    grid.zLabels = parseGridLabelLine("0 3600 7200");
    if (!editGridProperties(grid, QStringLiteral("Yapı Gridi Oluştur (Tekla tarzı)"))) return;
    app_.createModelGrid(std::move(grid));
}

void QtMainWindow::promptGridManager() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Yapı Gridleri"));
    dialog.resize(560, 360);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("Modeldeki yapı gridleri. Bir satıra çift tıklayın ya da\n"
                       "Düzenle'ye basın — Tekla'da grid çizgisine çift tıklamanın karşılığı."),
        &dialog));
    QListWidget* list = new QListWidget(&dialog);
    layout->addWidget(list, 1);

    const auto refresh = [this, list]() {
        QSignalBlocker block(list);
        list->clear();
        const auto& grids = app_.document().grids();
        for (std::size_t i = 0; i < grids.size(); ++i) {
            const GridDefinition& g = grids[i];
            QString summary = QStringLiteral("%1  —  X:%2  Y:%3  Z:%4")
                                  .arg(QString::fromStdString(g.name))
                                  .arg(g.xCoords.size())
                                  .arg(g.yCoords.size())
                                  .arg(g.zCoords.size());
            if (!g.visible) summary += QStringLiteral("  (gizli)");
            QListWidgetItem* item = new QListWidgetItem(summary, list);
            item->setData(Qt::UserRole, static_cast<int>(i));
        }
    };
    refresh();

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* newBtn = new QPushButton(QStringLiteral("Yeni..."), &dialog);
    QPushButton* editBtn = new QPushButton(QStringLiteral("Düzenle..."), &dialog);
    QPushButton* delBtn = new QPushButton(QStringLiteral("Sil"), &dialog);
    QPushButton* visBtn = new QPushButton(QStringLiteral("Görünürlük"), &dialog);
    buttons->addWidget(newBtn);
    buttons->addWidget(editBtn);
    buttons->addWidget(delBtn);
    buttons->addWidget(visBtn);
    buttons->addStretch(1);
    QDialogButtonBox* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->addWidget(closeBox);
    layout->addLayout(buttons);

    const auto selectedIndex = [list]() -> std::optional<std::size_t> {
        QListWidgetItem* item = list->currentItem();
        if (!item) return std::nullopt;
        return static_cast<std::size_t>(item->data(Qt::UserRole).toInt());
    };

    QObject::connect(newBtn, &QPushButton::clicked, &dialog, [this, &refresh]() {
        promptCreateGrid();
        refresh();
    });
    const auto editSelected = [this, &selectedIndex, &refresh]() {
        const std::optional<std::size_t> index = selectedIndex();
        if (!index || *index >= app_.document().grids().size()) return;
        GridDefinition grid = app_.document().grids()[*index];
        if (grid.axes.empty() && grid.xCoords.empty() && grid.yCoords.empty()) return;
        if (!editGridProperties(grid, QStringLiteral("Yapı Gridini Düzenle"))) return;
        app_.updateModelGrid(*index, std::move(grid));
        refresh();
    };
    QObject::connect(editBtn, &QPushButton::clicked, &dialog, editSelected);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&editSelected](QListWidgetItem*) { editSelected(); });
    QObject::connect(delBtn, &QPushButton::clicked, &dialog, [this, &selectedIndex, &refresh]() {
        const std::optional<std::size_t> index = selectedIndex();
        if (!index) return;
        app_.removeModelGrid(*index);
        refresh();
    });
    QObject::connect(visBtn, &QPushButton::clicked, &dialog, [this, &selectedIndex, &refresh]() {
        const std::optional<std::size_t> index = selectedIndex();
        if (!index || *index >= app_.document().grids().size()) return;
        const bool visible = app_.document().grids()[*index].visible;
        app_.setModelGridVisible(*index, !visible);
        refresh();
    });
    QObject::connect(closeBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

bool QtMainWindow::editGridProperties(GridDefinition& grid, const QString& title) {
    // TEKLA GRID PROPERTY PANE karsiligi. Alan adlari Tekla dokumaniyla
    // birebir: Coordinates (X/Y/Z), Labels (X/Y/Z), Line extensions
    // (Left/Below - Right/Above), Origin (X/Y/Z), Magnetic grid plane,
    // Grid color, Label font size and color, Name.
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    QFormLayout* form = new QFormLayout(&dialog);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);

    const auto section = [&dialog, form](const QString& text) {
        QLabel* label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), &dialog);
        form->addRow(label);
    };
    const auto coordText = [](const std::vector<double>& v, bool cumulative) {
        QStringList parts;
        double prev = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const double d = (cumulative && i > 0) ? v[i] - prev : v[i];
            prev = v[i];
            parts << QString::number(d);
        }
        return parts.join(QLatin1Char(' '));
    };
    const auto labelText = [](const std::vector<std::string>& v) {
        QStringList parts;
        for (const auto& s : v) parts << QString::fromStdString(s);
        return parts.join(QLatin1Char(' '));
    };

    section(QStringLiteral("Name"));
    QLineEdit* nameEdit = new QLineEdit(QString::fromStdString(grid.name), &dialog);
    form->addRow(QStringLiteral("Name:"), nameEdit);

    section(QStringLiteral("Coordinates  (X/Y artımsal, Z mutlak)"));
    QLineEdit* xEdit = new QLineEdit(coordText(grid.xCoords, true), &dialog);
    xEdit->setMinimumWidth(320);
    form->addRow(QStringLiteral("X (mm):"), xEdit);
    QLineEdit* yEdit = new QLineEdit(coordText(grid.yCoords, true), &dialog);
    form->addRow(QStringLiteral("Y (mm):"), yEdit);
    QLineEdit* zEdit = new QLineEdit(coordText(grid.zCoords, false), &dialog);
    form->addRow(QStringLiteral("Z (mm):"), zEdit);
    form->addRow(new QLabel(QStringLiteral(
        "Tekla yazımı: boşlukla ayrılmış değerler; \"2*4000\" iki tekrar demektir.\n"
        "Örnek: \"0 3*6000\" → 0, 6000, 12000, 18000."), &dialog));

    section(QStringLiteral("Labels  (boş bırakılabilir)"));
    QLineEdit* xLabEdit = new QLineEdit(labelText(grid.xLabels), &dialog);
    form->addRow(QStringLiteral("X etiketleri:"), xLabEdit);
    QLineEdit* yLabEdit = new QLineEdit(labelText(grid.yLabels), &dialog);
    form->addRow(QStringLiteral("Y etiketleri:"), yLabEdit);
    QLineEdit* zLabEdit = new QLineEdit(labelText(grid.zLabels), &dialog);
    form->addRow(QStringLiteral("Z (kot) etiketleri:"), zLabEdit);

    section(QStringLiteral("Line extensions (mm)"));
    QDoubleSpinBox* extLB = new QDoubleSpinBox(&dialog);
    extLB->setRange(-1000000.0, 1000000.0);
    extLB->setDecimals(1);
    extLB->setValue(grid.extendLeft);
    form->addRow(QStringLiteral("Left / Below:"), extLB);
    QDoubleSpinBox* extRA = new QDoubleSpinBox(&dialog);
    extRA->setRange(-1000000.0, 1000000.0);
    extRA->setDecimals(1);
    extRA->setValue(grid.extendRight);
    form->addRow(QStringLiteral("Right / Above:"), extRA);

    section(QStringLiteral("Origin (mm)"));
    QDoubleSpinBox* ox = new QDoubleSpinBox(&dialog);
    ox->setRange(-1000000.0, 1000000.0);
    ox->setDecimals(1);
    ox->setValue(grid.origin.x);
    form->addRow(QStringLiteral("X:"), ox);
    QDoubleSpinBox* oy = new QDoubleSpinBox(&dialog);
    oy->setRange(-1000000.0, 1000000.0);
    oy->setDecimals(1);
    oy->setValue(grid.origin.y);
    form->addRow(QStringLiteral("Y:"), oy);
    QDoubleSpinBox* oz = new QDoubleSpinBox(&dialog);
    oz->setRange(-1000000.0, 1000000.0);
    oz->setDecimals(1);
    oz->setValue(grid.origin.z);
    form->addRow(QStringLiteral("Z:"), oz);

    section(QStringLiteral("Appearance"));
    QCheckBox* magneticCheck = new QCheckBox(QStringLiteral("Magnetic grid plane"), &dialog);
    magneticCheck->setChecked(grid.magnetic);
    form->addRow(magneticCheck);
    QCheckBox* visibleCheck = new QCheckBox(QStringLiteral("Görünür"), &dialog);
    visibleCheck->setChecked(grid.visible);
    form->addRow(visibleCheck);

    unsigned int axisColor = grid.color;
    unsigned int labelColor = grid.labelColor;
    QPushButton* colorBtn = new QPushButton(&dialog);
    colorBtn->setStyleSheet(QStringLiteral("background-color:#%1;")
                                .arg(axisColor & 0xFFFFFFu, 6, 16, QLatin1Char('0')));
    form->addRow(QStringLiteral("Grid color:"), colorBtn);
    QObject::connect(colorBtn, &QPushButton::clicked, &dialog, [&dialog, &axisColor, colorBtn]() {
        const QColor c = QColorDialog::getColor(
            QColor(static_cast<int>((axisColor >> 16) & 0xFFu),
                   static_cast<int>((axisColor >> 8) & 0xFFu),
                   static_cast<int>(axisColor & 0xFFu)),
            &dialog, QStringLiteral("Grid color"));
        if (!c.isValid()) return;
        axisColor = static_cast<unsigned int>((c.red() << 16) | (c.green() << 8) | c.blue());
        colorBtn->setStyleSheet(QStringLiteral("background-color:#%1;")
                                    .arg(axisColor & 0xFFFFFFu, 6, 16, QLatin1Char('0')));
    });
    QSpinBox* fontSpin = new QSpinBox(&dialog);
    fontSpin->setRange(6, 72);
    fontSpin->setValue(grid.labelFontSize);
    form->addRow(QStringLiteral("Label font size (px):"), fontSpin);
    QPushButton* labelColorBtn = new QPushButton(&dialog);
    labelColorBtn->setStyleSheet(QStringLiteral("background-color:#%1;")
                                     .arg(labelColor & 0xFFFFFFu, 6, 16, QLatin1Char('0')));
    form->addRow(QStringLiteral("Label color:"), labelColorBtn);
    QObject::connect(labelColorBtn, &QPushButton::clicked, &dialog,
                     [&dialog, &labelColor, labelColorBtn]() {
        const QColor c = QColorDialog::getColor(
            QColor(static_cast<int>((labelColor >> 16) & 0xFFu),
                   static_cast<int>((labelColor >> 8) & 0xFFu),
                   static_cast<int>(labelColor & 0xFFu)),
            &dialog, QStringLiteral("Label color"));
        if (!c.isValid()) return;
        labelColor = static_cast<unsigned int>((c.red() << 16) | (c.green() << 8) | c.blue());
        labelColorBtn->setStyleSheet(QStringLiteral("background-color:#%1;")
                                         .arg(labelColor & 0xFFFFFFu, 6, 16, QLatin1Char('0')));
    });

    QDialogButtonBox* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return false;

    grid.name = nameEdit->text().trimmed().toStdString();
    grid.xCoords = parseGridCoordinateLine(xEdit->text().toStdString(), true);
    grid.yCoords = parseGridCoordinateLine(yEdit->text().toStdString(), true);
    grid.zCoords = parseGridCoordinateLine(zEdit->text().toStdString(), false);
    grid.xLabels = parseGridLabelLine(xLabEdit->text().toStdString());
    grid.yLabels = parseGridLabelLine(yLabEdit->text().toStdString());
    grid.zLabels = parseGridLabelLine(zLabEdit->text().toStdString());
    grid.extendLeft = extLB->value();
    grid.extendRight = extRA->value();
    grid.extendBelow = extLB->value();
    grid.extendAbove = extRA->value();
    grid.origin = Vec3{ox->value(), oy->value(), oz->value()};
    grid.magnetic = magneticCheck->isChecked();
    grid.visible = visibleCheck->isChecked();
    grid.color = axisColor;
    grid.labelColor = labelColor;
    grid.labelFontSize = fontSpin->value();
    return true;
}

void QtMainWindow::createToolbar() {
    QToolBar* ribbon = addToolBar("Ribbon");
    ribbon->setMovable(false);
    ribbon->setStyleSheet(
        "QToolBar { background: #2B2B30; border: none; spacing: 2px; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #1E1E24; color: #B8B8C0; padding: 4px 12px; }"
        "QTabBar::tab:selected { background: #2B2B30; color: #FFFFFF; }");

    // Tekla tarzi sol ust kose MENU dugmesi: tum uygulama menuleri buradan
    // acilir (menubar gizli). appMenu_ createMenus'ta kuruldu.
    if (appMenu_) {
        QToolButton* menuBtn = new QToolButton(ribbon);
        menuBtn->setText(QStringLiteral("☰"));
        menuBtn->setToolTip(QStringLiteral("Menü"));
        menuBtn->setPopupMode(QToolButton::InstantPopup);
        menuBtn->setMenu(appMenu_);
        menuBtn->setStyleSheet(
            "QToolButton { font-size: 12pt; color: #E8E8E8; border: none; "
            "border-radius: 3px; padding: 2px 10px; background: transparent; }"
            "QToolButton:hover { background: #3C3C44; }"
            "QToolButton::menu-indicator { image: none; }");
        ribbon->addWidget(menuBtn);
    }

    QTabWidget* tabs = new QTabWidget(ribbon);
    tabs->setDocumentMode(true);
    tabs->setTabPosition(QTabWidget::North);

    // ---- Ribbon sekmeleri: createMenus ile AYNI tablodan (commandCategories) ----
    // Her menü kategorisi = bir ribbon sekmesi, aynı sıra ve aynı içerik.
    // Grup = açılır düğme, ayırıcı = ince dikey çizgi. Yalnız-menü kategoriler
    // (Dosya, Pencere) sekme açmaz.
    const QString tabBtnStyle = QStringLiteral(
        "QToolButton { font-size: 7.5pt; color: #E8E8E8; border: none; border-radius: 3px; background: transparent; }"
        "QToolButton:hover { background: #3C3C44; }"
        "QToolButton:pressed { background: #23232B; }"
        "QToolButton::menu-indicator { image: none; }");
    for (const CommandCategory& cat : commandCategories()) {
        if (cat.tabTitle.isEmpty()) continue;
        QWidget* page = new QWidget();
        QHBoxLayout* lay = new QHBoxLayout(page);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(2);
        for (const UiEntry& e : cat.entries) {
            if (e.kind == UiEntry::Kind::Separator) {
                QFrame* line = new QFrame();
                line->setFrameShape(QFrame::VLine);
                line->setFrameShadow(QFrame::Plain);
                line->setStyleSheet(QStringLiteral("color: #3C3C44;"));
                lay->addWidget(line);
                continue;
            }
            QToolButton* btn = new QToolButton();
            btn->setIcon(makeToolIcon(e.cmd.glyph));
            btn->setIconSize(QSize(22, 22));
            btn->setText(e.cmd.ribbonText.isEmpty() ? plainCommandLabel(e.cmd.menuText) : e.cmd.ribbonText);
            btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            btn->setFixedSize(58, 48);
            btn->setToolTip(e.cmd.tip.isEmpty() ? plainCommandLabel(e.cmd.menuText) : e.cmd.tip);
            btn->setStyleSheet(tabBtnStyle);
            if (e.kind == UiEntry::Kind::Group) {
                btn->setPopupMode(QToolButton::InstantPopup);
                QMenu* menu = new QMenu(btn);
                // Kısayollar yalnız ana menüye bağlanır; açılır listede metin
                // olarak gösterilir (aynı tuşa iki eylem = belirsiz kısayol).
                std::vector<UiEntry> shown = e.children;
                for (UiEntry& c : shown) c.cmd.bindShortcut = false;
                fillUiMenu(menu, this, shown, false);
                btn->setMenu(menu);
            } else {
                QObject::connect(btn, &QToolButton::clicked, this, e.cmd.run);
            }
            lay->addWidget(btn);
        }
        lay->addStretch();
        tabs->addTab(page, cat.tabTitle);
    }

    ribbon->addWidget(tabs);

    // ---- Persistent controls (right side, always visible) ----
    // Spacer to push everything right
    QWidget* spacer = new QWidget(ribbon);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ribbon->addWidget(spacer);

    // Quick Launch (Tekla tarzi): komut adi yaz -> Enter calistir.
    QLineEdit* quick = new QLineEdit(ribbon);
    quick->setPlaceholderText(QStringLiteral("Komut ara..."));
    quick->setFixedWidth(160);
    quick->setClearButtonEnabled(true);
    quick->setStyleSheet(
        "QLineEdit { background: #1E1E24; color: #E8E8E8; border: 1px solid #3C3C44; "
        "border-radius: 3px; padding: 3px 6px; }");
    // Komut listesi menü/ribbon tablosundan üretilir (her komut aranabilir) +
    // birkaç Tekla takma adı.
    struct QuickCmd { QString label; std::function<void()> run; };
    std::vector<QuickCmd> cmds = {
        {QStringLiteral("Kiriş"),          [this]() { app_.selectTool(DrawTool::Line); }},
        {QStringLiteral("Yapı Gridi"),     [this]() { promptCreateGrid(); }},
        {QStringLiteral("Ön Görünüş"),     [this]() { app_.setStandardView(StandardView::Front); }},
        {QStringLiteral("Odaklan"),        [this]() { app_.focusOnSelection(); }},
    };
    {
        std::vector<std::pair<QString, std::function<void()>>> fromTable;
        for (const CommandCategory& cat : commandCategories())
            collectQuickCommands(cat.entries, fromTable);
        for (auto& [label, run] : fromTable) {
            if (label.isEmpty()) continue;
            const bool dup = std::any_of(cmds.begin(), cmds.end(), [&](const QuickCmd& q) {
                return q.label.compare(label, Qt::CaseInsensitive) == 0; });
            if (!dup) cmds.push_back({label, run});
        }
    }
    QStringList cmdNames;
    for (const auto& c : cmds) cmdNames << c.label;
    QCompleter* quickCompleter = new QCompleter(cmdNames, quick);
    quickCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    quickCompleter->setFilterMode(Qt::MatchContains);
    quick->setCompleter(quickCompleter);
    QObject::connect(quick, &QLineEdit::returnPressed, this, [this, quick, cmds]() {
        const QString t = quick->text().trimmed();
        for (const auto& c : cmds)
            if (c.label.compare(t, Qt::CaseInsensitive) == 0) { c.run(); break; }
        quick->clear();
        if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);
    });
    ribbon->addWidget(quick);

    // Snap toggle with right-click context menu for snap modes
    QPushButton* snapBtn = new QPushButton("\xF0\x9F\x94\xB2 Snap", ribbon);
    snapBtn->setCheckable(true);
    snapBtn->setChecked(app_.snapEnabled());
    snapBtn->setFixedSize(65, 28);
    snapBtn->setStyleSheet("QPushButton { font-size: 8pt; }");
    snapBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(snapBtn, &QPushButton::clicked, this, [this, snapBtn]() {
        app_.setSnapEnabled(snapBtn->isChecked());
    });
    QObject::connect(snapBtn, &QPushButton::customContextMenuRequested, this, [this, snapBtn](const QPoint& pos) {
        QMenu menu(snapBtn);
        QAction* gridAct = menu.addAction("Grid Snap");
        gridAct->setCheckable(true);
        gridAct->setChecked(app_.gridSnapEnabled());
        QObject::connect(gridAct, &QAction::triggered, this, [this, gridAct]() {
            app_.setGridSnapEnabled(gridAct->isChecked());
        });
        menu.addSeparator();
        const auto& mask = app_.enabledSnapTypes();
        auto addSnapItem = [&](const QString& label, SnapType type) {
            QAction* act = menu.addAction(label);
            act->setCheckable(true);
            std::size_t idx = static_cast<std::size_t>(type);
            act->setChecked(idx < mask.size() && mask[idx]);
            QObject::connect(act, &QAction::triggered, this, [this, type]() { app_.toggleSnapType(type); });
        };
        addSnapItem("🔷 Endpoint",       SnapType::Endpoint);
        addSnapItem("🔶 Midpoint",       SnapType::Midpoint);
        addSnapItem("⏺  Center",         SnapType::Center);
        addSnapItem("⊞ Intersection",    SnapType::Intersection);
        addSnapItem("⫡ Nearest",         SnapType::Nearest);
        addSnapItem("⫡ Perpendicular",   SnapType::Perpendicular);
        addSnapItem("∠ Tangent",         SnapType::Tangent);
        addSnapItem("● Node",            SnapType::Node);
        addSnapItem("∥ Parallel",        SnapType::Parallel);
        addSnapItem("⊙ Profile Center",  SnapType::ProfileCenter);
        menu.exec(snapBtn->mapToGlobal(pos));
    });
    ribbon->addWidget(snapBtn);

    // Ortho toggle
    QPushButton* orthoBtn = new QPushButton("\xE2\x8A\xA5 Ortho", ribbon);
    orthoBtn->setCheckable(true);
    orthoBtn->setChecked(app_.orthoEnabled());
    orthoBtn->setFixedSize(70, 28);
    orthoBtn->setStyleSheet("QPushButton { font-size: 8pt; }");
    QObject::connect(orthoBtn, &QPushButton::clicked, this, [this, orthoBtn]() {
        app_.setOrthoEnabled(orthoBtn->isChecked());
    });
    ribbon->addWidget(orthoBtn);

    // PART / ASSEMBLY secim modu (Phase W2): birbirini dislayan iki checkable
    // dugme. Part = yalniz tiklanan parca; Assembly = tiklanan parcanin tum
    // assembly uyeleri (main + secondary + levhalar + weld'ler) birlikte secilir.
    QPushButton* partBtn = new QPushButton(QStringLiteral("Parça"), ribbon);
    partBtn->setCheckable(true);
    partBtn->setChecked(app_.selectionMode() == mm::Application::SelectionMode::Part);
    partBtn->setFixedSize(56, 28);
    partBtn->setToolTip(QStringLiteral("Select Part: yalnız tıklanan parça seçilir"));
    partBtn->setStyleSheet(QStringLiteral("QPushButton { font-size: 8pt; }"));
    QPushButton* asmBtn = new QPushButton(QStringLiteral("Assembly"), ribbon);
    asmBtn->setCheckable(true);
    asmBtn->setChecked(app_.selectionMode() == mm::Application::SelectionMode::Assembly);
    asmBtn->setFixedSize(74, 28);
    asmBtn->setToolTip(QStringLiteral("Select Assembly: tıklanan parçanın tüm üyeleri seçilir"));
    asmBtn->setStyleSheet(QStringLiteral("QPushButton { font-size: 8pt; }"));
    QObject::connect(partBtn, &QPushButton::clicked, this, [this, partBtn, asmBtn]() {
        asmBtn->setChecked(false);
        partBtn->setChecked(true);
        app_.setSelectionMode(mm::Application::SelectionMode::Part);
    });
    QObject::connect(asmBtn, &QPushButton::clicked, this, [this, partBtn, asmBtn]() {
        partBtn->setChecked(false);
        asmBtn->setChecked(true);
        app_.setSelectionMode(mm::Application::SelectionMode::Assembly);
    });
    ribbon->addWidget(partBtn);
    ribbon->addWidget(asmBtn);

    // Color palette button
    QPushButton* colorBtn = new QPushButton("\xF0\x9F\x8E\xA8", ribbon);
    colorBtn->setFixedSize(36, 28);
    colorBtn->setToolTip("Renk seç");
    colorBtn->setStyleSheet("QPushButton { font-size: 10pt; }");
    QObject::connect(colorBtn, &QPushButton::clicked, this, [this, colorBtn]() {
        QColor color = QColorDialog::getColor(Qt::white, this, "Renk Seç");
        if (color.isValid()) {
            // Map selected RGB to closest palette entry
            const auto& palette = Application::colorPalette();
            int bestIdx = 0;
            int bestDist = 0x7FFFFFFF;
            for (std::size_t i = 0; i < palette.size(); ++i) {
                if (!palette[i].second) continue; // ByLayer
                uint32_t c = *palette[i].second;
                int dr = static_cast<int>((c >> 16) & 0xFF) - color.red();
                int dg = static_cast<int>((c >>  8) & 0xFF) - color.green();
                int db = static_cast<int>((c >>  0) & 0xFF) - color.blue();
                int dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) { bestDist = dist; bestIdx = static_cast<int>(i); }
            }
            app_.setCurrentColorChoice(bestIdx);
            QString style = QString("QPushButton { font-size: 10pt; background-color: %1; border-radius: 3px; }")
                .arg(color.name());
            colorBtn->setStyleSheet(style);
        }
    });
    ribbon->addWidget(colorBtn);

    // Line type dropdown
    QComboBox* ltypeCombo = new QComboBox(ribbon);
    ltypeCombo->setFixedWidth(110);
    ltypeCombo->addItem("\xE2\x94\x81 Continuous");
    ltypeCombo->addItem("\xE2\x95\x8C Hidden");
    ltypeCombo->addItem("\xC2\xB7 Center");
    ltypeCombo->addItem("\xE2\x94\x80 Phantom");
    ltypeCombo->addItem("\xE2\x95\x90 Dashed");
    ltypeCombo->setCurrentIndex(std::max(0, app_.currentLineTypeChoice()));
    QObject::connect(ltypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int idx) { app_.setCurrentLineTypeChoice(idx); });
    ribbon->addWidget(ltypeCombo);

    // Layer dropdown + "..." button
    QComboBox* layerCombo = new QComboBox(ribbon);
    layerCombo->setFixedWidth(100);
    {
        auto names = app_.layerNames();
        for (const auto& n : names) layerCombo->addItem(QString::fromStdString(n));
        layerCombo->setCurrentText(QString::fromStdString(app_.currentLayer()));
    }
    app_.setLayerComboWidget(layerCombo); // Qt modu: stil state uygulama uyelerinden okunur
    QObject::connect(layerCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
        this, [this](const QString& text) { app_.setCurrentLayer(text.toStdString()); });
    ribbon->addWidget(layerCombo);

    QPushButton* layerBtn = new QPushButton("\xE2\x8B\xAF", ribbon);
    layerBtn->setFixedSize(28, 28);
    layerBtn->setToolTip("Katman yöneticisi");
    layerBtn->setStyleSheet("QPushButton { font-size: 10pt; font-weight: bold; }");
    QObject::connect(layerBtn, &QPushButton::clicked, this, [this, layerCombo]() {
        QDialog* dlg = new QDialog(this);
        dlg->setWindowTitle("Katman Yöneticisi");
        dlg->setMinimumSize(600, 350);
        QVBoxLayout* lay = new QVBoxLayout(dlg);

        // Search bar
        QHBoxLayout* searchRow = new QHBoxLayout();
        QLabel* searchLabel = new QLabel("Ara:", dlg);
        QLineEdit* searchEdit = new QLineEdit(dlg);
        searchEdit->setPlaceholderText("Katman adı filtrele...");
        searchRow->addWidget(searchLabel);
        searchRow->addWidget(searchEdit);
        lay->addLayout(searchRow);

        // Table: Current, Name, On, Freeze, Lock, Color, Linetype
        QTableWidget* table = new QTableWidget(dlg);
        table->setColumnCount(7);
        QStringList headers;
        headers << "" << "Ad" << "Açık" << "Don" << "Kilit" << "Renk" << "Çizgi Tipi";
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked);
        table->verticalHeader()->hide();

        // Populate from document layers.
        auto populateTable = [&](const QString& filter = {}) {
            const auto& layers = app_.layerProperties();
            int total = (int)layers.size();

            // Sort: current layer first, then alphabet...
            std::vector<std::pair<std::string, EntityProperties>> sorted(
                layers.begin(), layers.end());
            std::string curLayer = app_.currentLayer();
            std::sort(sorted.begin(), sorted.end(), [&](auto& a, auto& b) {
                if (a.first == curLayer) return true;
                if (b.first == curLayer) return false;
                return a.first < b.first;
            });

            table->setSortingEnabled(false);
            table->setRowCount(total);
            table->setSortingEnabled(true);

            int visible = 0;
            for (int idx = 0; idx < (int)sorted.size(); ++idx) {
                const auto& [name, props] = sorted[idx];
                QString qname = QString::fromStdString(name);
                if (!filter.isEmpty() && !qname.contains(filter, Qt::CaseInsensitive))
                    continue;
                int row = visible++;

                // Current marker
                bool isCurrent = (name == curLayer);
                auto* curItem = new QTableWidgetItem(isCurrent ? "▶" : "");
                curItem->setFlags(curItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 0, curItem);

                // Name
                auto* nameItem = new QTableWidgetItem(qname);
                nameItem->setData(Qt::UserRole, qname);
                table->setItem(row, 1, nameItem);

                // On
                auto* onItem = new QTableWidgetItem();
                onItem->setCheckState(props.visible ? Qt::Checked : Qt::Unchecked);
                onItem->setFlags(onItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 2, onItem);

                // Freeze
                auto* freezeItem = new QTableWidgetItem();
                freezeItem->setCheckState(props.frozen ? Qt::Checked : Qt::Unchecked);
                freezeItem->setFlags(freezeItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 3, freezeItem);

                // Lock
                auto* lockItem = new QTableWidgetItem();
                lockItem->setCheckState(props.locked ? Qt::Checked : Qt::Unchecked);
                lockItem->setFlags(lockItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 4, lockItem);

                // Color
                QColor color(props.effectiveColor & 0xFF, (props.effectiveColor >> 8) & 0xFF,
                             (props.effectiveColor >> 16) & 0xFF);
                auto* colorItem = new QTableWidgetItem();
                colorItem->setBackground(color);
                colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 5, colorItem);

                // Linetype
                auto* ltItem = new QTableWidgetItem(QString::fromStdString(props.effectiveLineType));
                ltItem->setFlags(ltItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 6, ltItem);
            }

            // Hide empty trailing rows
            for (int r = visible; r < table->rowCount(); ++r) {
                table->setRowHidden(r, true);
            }
            for (int r = 0; r < visible; ++r) {
                table->setRowHidden(r, false);
            }
        };
        populateTable();
        QObject::connect(searchEdit, &QLineEdit::textChanged, dlg, [&](const QString& text) {
            populateTable(text);
        });
        lay->addWidget(table);

        // Status bar
        QLabel* statusLabel = new QLabel(dlg);
        auto updateStatus = [&]() {
            auto names = app_.layerNames();
            statusLabel->setText(QString("Toplam katman: %1").arg(names.size()));
        };
        updateStatus();
        lay->addWidget(statusLabel);

        // Button row
        QHBoxLayout* btnRow = new QHBoxLayout();
        QPushButton* curBtn  = new QPushButton("▶ Aktif Yap", dlg);
        QPushButton* addBtn  = new QPushButton("+ Yeni", dlg);
        QPushButton* delBtn  = new QPushButton("✕ Sil", dlg);
        QPushButton* renBtn  = new QPushButton("✎ Ad Değiştir", dlg);
        btnRow->addWidget(curBtn);
        btnRow->addWidget(addBtn);
        btnRow->addWidget(delBtn);
        btnRow->addWidget(renBtn);
        btnRow->addStretch();

        // Activate: make selected layer current — update ▶ markers inline
        QObject::connect(curBtn, &QPushButton::clicked, dlg, [&]() {
            int r = table->currentRow();
            if (r < 0) return;
            QString name = table->item(r, 1)->text();
            app_.setCurrentLayer(name.toStdString());
            // Move ▶ marker: clear old, set new
            for (int i = 0; i < table->rowCount(); ++i) {
                auto* it = table->item(i, 0);
                if (it) it->setText(table->item(i, 1)->text() == name ? "▶" : "");
            }
        });

        // New layer — insert single row without full table rebuild
        QObject::connect(addBtn, &QPushButton::clicked, dlg, [&]() {
            // Auto-generate layer name
            auto names = app_.layerNames();
            int next = 1;
            while (true) {
                std::string candidate = "Layer_" + std::to_string(next);
                bool found = false;
                for (const auto& n : names) { if (n == candidate) { found = true; break; } }
                if (!found) break;
                next++;
            }
            std::string sname = "Layer_" + std::to_string(next);
            if (!app_.createLayer(sname)) return;

            // Get properties of the new layer
            auto props = app_.layerProperties();
            auto it = props.find(sname);
            if (it == props.end()) return;
            const auto& p = it->second;

            // Insert one row at the end
            int row = table->rowCount();
            table->setRowCount(row + 1);

            QString qname = QString::fromStdString(sname);
            bool isCurrent = (sname == app_.currentLayer());

            auto* curItem = new QTableWidgetItem(isCurrent ? "▶" : "");
            curItem->setFlags(curItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 0, curItem);

            auto* nameItem = new QTableWidgetItem(qname);
            nameItem->setData(Qt::UserRole, qname);
            table->setItem(row, 1, nameItem);

            auto* onItem = new QTableWidgetItem();
            onItem->setCheckState(p.visible ? Qt::Checked : Qt::Unchecked);
            onItem->setFlags(onItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 2, onItem);

            auto* freezeItem = new QTableWidgetItem();
            freezeItem->setCheckState(p.frozen ? Qt::Checked : Qt::Unchecked);
            freezeItem->setFlags(freezeItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 3, freezeItem);

            auto* lockItem = new QTableWidgetItem();
            lockItem->setCheckState(p.locked ? Qt::Checked : Qt::Unchecked);
            lockItem->setFlags(lockItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 4, lockItem);

            QColor color(p.effectiveColor & 0xFF, (p.effectiveColor >> 8) & 0xFF,
                         (p.effectiveColor >> 16) & 0xFF);
            auto* colorItem = new QTableWidgetItem();
            colorItem->setBackground(color);
            colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 5, colorItem);

            auto* ltItem = new QTableWidgetItem(QString::fromStdString(p.effectiveLineType));
            ltItem->setFlags(ltItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 6, ltItem);

            updateStatus();
        });

        // Delete layer — remove row directly
        QObject::connect(delBtn, &QPushButton::clicked, dlg, [&]() {
            int r = table->currentRow();
            if (r < 0) return;
            QString name = table->item(r, 1)->text();
            if (name == "0") {
                QMessageBox::warning(dlg, "Hata", "Varsayılan katman (0) silinemez.");
                return;
            }
            if (!app_.deleteLayer(name.toStdString())) {
                QMessageBox::warning(dlg, "Hata",
                    QString("'%1' katmanı silinemedi (üzerinde nesne olabilir).").arg(name));
                return;
            }
            table->removeRow(r);
            updateStatus();
        });

        // Rename layer
        QObject::connect(renBtn, &QPushButton::clicked, dlg, [&]() {
            int r = table->currentRow();
            if (r < 0) return;
            QString oldName = table->item(r, 1)->text();
            if (oldName == "0") {
                QMessageBox::warning(dlg, "Hata", "Varsayılan katman (0) yeniden adlandırılamaz.");
                return;
            }
            bool ok = false;
            QString newName = QInputDialog::getText(dlg, "Katman Adı Değiştir",
                QString("'%1' için yeni ad:").arg(oldName),
                QLineEdit::Normal, oldName, &ok);
            if (ok && !newName.trimmed().isEmpty() && newName.trimmed() != oldName) {
                if (!app_.renameLayer(oldName.toStdString(), newName.trimmed().toStdString())) {
                    QMessageBox::warning(dlg, "Hata",
                        QString("'%1' → '%2' olarak değiştirilemedi.").arg(oldName, newName.trimmed()));
                    return;
                }
                // Update just the name cell
                auto* it = table->item(r, 1);
                if (it) it->setText(newName.trimmed());
            }
        });

        // Double-click to activate — move ▶ inline
        QObject::connect(table, &QTableWidget::cellDoubleClicked, dlg, [&](int row, int) {
            if (row < 0) return;
            QString name = table->item(row, 1)->text();
            app_.setCurrentLayer(name.toStdString());
            for (int i = 0; i < table->rowCount(); ++i) {
                auto* it = table->item(i, 0);
                if (it) it->setText(table->item(i, 1)->text() == name ? "▶" : "");
            }
        });

        lay->addLayout(btnRow);

        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok, dlg);
        QObject::connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
        lay->addWidget(bb);

        dlg->exec();
        dlg->deleteLater();

        // Defer combo sync — dialog cleanup must finish first
        QTimer::singleShot(0, this, [this, layerCombo]() {
            QString currentLayer = QString::fromStdString(app_.currentLayer());
            if (layerCombo->currentText() != currentLayer) {
                layerCombo->blockSignals(true);
                layerCombo->setCurrentText(currentLayer);
                layerCombo->blockSignals(false);
            }
        });
    });
    ribbon->addWidget(layerBtn);

    // SAG DIKEY SERIT (Tekla tarzi): Yardim / Ayarlar / Modelleme kisa erisim.
    QToolBar* sideBar = new QToolBar(QStringLiteral("Hızlı Erişim"), this);
    sideBar->setOrientation(Qt::Vertical);
    sideBar->setMovable(false);
    sideBar->setIconSize(QSize(20, 20));
    auto addSideBtn = [&](const QString& text, const QString& tip, auto action) {
        QToolButton* b = new QToolButton(sideBar);
        b->setText(text);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedSize(46, 46);
        b->setStyleSheet(tabBtnStyle);
        QObject::connect(b, &QToolButton::clicked, this, action);
        sideBar->addWidget(b);
    };
    addSideBtn(QStringLiteral("?"), QStringLiteral("Yardım"), [this]() {
        QMessageBox::information(this, QStringLiteral("Model Maker"),
            QStringLiteral("Model Maker — Tekla tarzı çelik yapı CAD'i."));
    });
    addSideBtn(QStringLiteral("⚙"), QStringLiteral("Ayarlar"), [this]() {
        QMessageBox::information(this, QStringLiteral("Ayarlar"),
            QStringLiteral("Ayarlar paneli henüz eklenmedi."));
    });
    addSideBtn(QStringLiteral("M"), QStringLiteral("Modelleme"), [this]() {
        if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);
    });
    addToolBar(Qt::RightToolBarArea, sideBar);
}

void QtMainWindow::createDockPanels() {
    QDockWidget* propsDock = new QDockWidget("Özellikler", this);
    QWidget* propsWidget = new QWidget(propsDock);
    QVBoxLayout* propsLayout = new QVBoxLayout(propsWidget);
    propsLayout->setContentsMargins(8, 8, 8, 8);
    propsLayout->setSpacing(6);

    // Tip (salt okunur)
    QLabel* typeLabel = new QLabel("-", propsWidget);
    typeLabel->setObjectName("propsType");
    typeLabel->setStyleSheet("color: #9FA6B2; font-size: 9pt;");
    propsLayout->addWidget(typeLabel);

    // KAYNAK / ASSEMBLY bilgisi (salt okunur): weld veya assembly secildiginde
    // burada detay gosterilir (Phase W2). Model seciminde bos kalir.
    QLabel* weldAsmInfo = new QLabel(QString(), propsWidget);
    weldAsmInfo->setObjectName("propsWeldAsm");
    weldAsmInfo->setWordWrap(true);
    weldAsmInfo->setStyleSheet("color: #D0B37E; font-size: 8.5pt;");
    propsLayout->addWidget(weldAsmInfo);

    // Profil (AKTIF): secili nesnenin profilini gosterir/yazar; secim yokken
    // buradaki deger CIZILECEK kirisin profilidir. (Ust ribbon'daki ayri
    // profil cubugu kaldirildi — tek yer burasidir.)
    QLabel* profileCaption = new QLabel("Profil (aktif)", propsWidget);
    profileCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(profileCaption);
    QComboBox* propsProfile = new QComboBox(propsWidget);
    propsProfile->setObjectName("propsProfile");
    propsProfile->setEditable(true);
    propsProfile->setInsertPolicy(QComboBox::NoInsert);
    propsProfile->setMinimumContentsLength(18);
    propsProfile->setMaxVisibleItems(22);
    propsProfile->setToolTip(
        "Secim YOKKEN: buradaki profil cizilecek kirisi belirler (profilsiz = "
        "duz cizgi).\nSecim VARKEN: profili secime atar.\nX tusu bu alana odaklanir.");
    // Oge verisi (Qt::UserRole) = profil adi; yer tutucuda bos string.
    // Elle yazilan ad icin veri yoktur, metin okunur.
    propsProfile->addItem(profileNoneLabel(), QString());
    for (const auto& name : app_.profileNames())
        propsProfile->addItem(QString::fromStdString(name),
                              QString::fromStdString(name));
    if (QCompleter* completer = propsProfile->completer()) {
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
    }
    propsLayout->addWidget(propsProfile);
    // X tusu / CmdProfileButton: eski ribbon combo'su yerine bu alana odaklan.
    profileSelector_ = propsProfile;
    app_.setProfilePickerCallback([this]() {
        if (!profileSelector_ || !profileSelector_->isEnabled()) return;
        QTimer::singleShot(0, profileSelector_, [this]() {
            if (!profileSelector_) return;
            profileSelector_->setFocus(Qt::ShortcutFocusReason);
            profileSelector_->showPopup();
        });
    });

    // Katman
    QLabel* layerCaption = new QLabel("Katman", propsWidget);
    layerCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(layerCaption);
    QComboBox* propsLayer = new QComboBox(propsWidget);
    propsLayer->setObjectName("propsLayer");
    propsLayout->addWidget(propsLayer);

    // Renk
    QLabel* colorCaption = new QLabel("Renk", propsWidget);
    colorCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(colorCaption);
    QComboBox* propsColor = new QComboBox(propsWidget);
    propsColor->setObjectName("propsColor");
    propsLayout->addWidget(propsColor);

    // Çizgi Tipi
    QLabel* lineTypeCaption = new QLabel("Çizgi Tipi", propsWidget);
    lineTypeCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(lineTypeCaption);
    QComboBox* propsLineType = new QComboBox(propsWidget);
    propsLineType->setObjectName("propsLineType");
    propsLayout->addWidget(propsLineType);

    // Malzeme
    QLabel* materialCaption = new QLabel("Malzeme", propsWidget);
    materialCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(materialCaption);
    QComboBox* propsMaterial = new QComboBox(propsWidget);
    propsMaterial->setObjectName("propsMaterial");
    propsMaterial->setEditable(true);
    propsMaterial->setInsertPolicy(QComboBox::NoInsert);
    propsLayout->addWidget(propsMaterial);

    // Profil rotasyonu (derece)
    QLabel* rotationCaption = new QLabel("Rotasyon (°)", propsWidget);
    rotationCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(rotationCaption);
    QDoubleSpinBox* propsRotation = new QDoubleSpinBox(propsWidget);
    propsRotation->setObjectName("propsRotation");
    propsRotation->setRange(0.0, 360.0);
    propsRotation->setDecimals(1);
    propsRotation->setSingleStep(5.0);
    propsRotation->setSuffix("°");
    propsLayout->addWidget(propsRotation);

    // Kolon ust/alt kot (mm) — kolon butonu icin.
    QLabel* topCaption = new QLabel("Kolon Üst (mm)", propsWidget);
    topCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(topCaption);
    QDoubleSpinBox* propsColumnTop = new QDoubleSpinBox(propsWidget);
    propsColumnTop->setObjectName("propsColumnTop");
    propsColumnTop->setRange(-100000.0, 100000.0);
    propsColumnTop->setDecimals(0);
    propsColumnTop->setValue(8500.0);
    propsLayout->addWidget(propsColumnTop);

    QLabel* bottomCaption = new QLabel("Kolon Alt (mm)", propsWidget);
    bottomCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(bottomCaption);
    QDoubleSpinBox* propsColumnBottom = new QDoubleSpinBox(propsWidget);
    propsColumnBottom->setObjectName("propsColumnBottom");
    propsColumnBottom->setRange(-100000.0, 100000.0);
    propsColumnBottom->setDecimals(0);
    propsColumnBottom->setValue(0.0);
    propsLayout->addWidget(propsColumnBottom);

    // Uzunluk (salt okunur)
    QLabel* lengthCaption = new QLabel("Uzunluk", propsWidget);
    lengthCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(lengthCaption);
    QLabel* lengthValue = new QLabel("-", propsWidget);
    lengthValue->setObjectName("propsLength");
    lengthValue->setStyleSheet("color: #F0F0F4; font-size: 9pt;");
    propsLayout->addWidget(lengthValue);

    QPushButton* propsApply = new QPushButton("UYGULA", propsWidget);
    propsApply->setObjectName("propsApply");
    propsApply->setStyleSheet(
        "QPushButton { background: #2E6B4F; color: #F0F0F4; font-weight: 700; "
        "padding: 6px; border-radius: 3px; }"
        "QPushButton:hover { background: #37805F; }");
    propsLayout->addWidget(propsApply);

    QPushButton* propsColumnBtn = new QPushButton("KOLON YERLEŞTİR", propsWidget);
    propsColumnBtn->setObjectName("propsColumnBtn");
    propsColumnBtn->setStyleSheet(
        "QPushButton { background: #3A5A8C; color: #F0F0F4; font-weight: 700; "
        "padding: 6px; border-radius: 3px; }"
        "QPushButton:hover { background: #486FAB; }");
    propsLayout->addWidget(propsColumnBtn);

    propsLayout->addStretch(1);
    propsDock->setWidget(propsWidget);
    propsDock->setMinimumWidth(250);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    // ---- MODEL AGACI (Tekla model organizer esdegeri) ----
    QDockWidget* treeDock = new QDockWidget(QStringLiteral("Model Ağacı"), this);
    treeDock->setObjectName("modelTreeDock");
    QWidget* treeWidget = new QWidget(treeDock);
    QVBoxLayout* treeLayout = new QVBoxLayout(treeWidget);
    treeLayout->setContentsMargins(4, 4, 4, 4);
    treeLayout->setSpacing(4);
    QLineEdit* treeSearch = new QLineEdit(treeWidget);
    treeSearch->setPlaceholderText(QStringLiteral("Ara..."));
    treeLayout->addWidget(treeSearch);
    QTreeWidget* tree = new QTreeWidget(treeWidget);
    tree->setHeaderHidden(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    treeLayout->addWidget(tree);
    treeDock->setWidget(treeWidget);
    treeDock->setMinimumWidth(220);
    addDockWidget(Qt::RightDockWidgetArea, treeDock);

    // Agac icerigi 500ms'de bir tazelenir (Application QObject degil, sinyal
    // yok). Yalniz sayi/filtre degisince yeniden kurulur; secim kalinligi her
    // turda guncellenir (kullanicinin yaptigi secim de yansir).
    QTimer* treeTimer = new QTimer(this);
    treeTimer->setInterval(500);
    QObject::connect(treeTimer, &QTimer::timeout, this,
        [this, tree, treeSearch]() {
            static int lastCount = -1;
            static std::size_t lastAsm = 0, lastWeld = 0;
            static QString lastFilter;
            const int count = app_.modelCount();
            const std::size_t asmCount = app_.assemblies().size();
            const std::size_t weldCount = app_.welds().size();
            const QString filter = treeSearch->text().trimmed();
            if (count != lastCount || asmCount != lastAsm || weldCount != lastWeld ||
                filter != lastFilter) {
                lastCount = count;
                lastAsm = asmCount;
                lastWeld = weldCount;
                lastFilter = filter;
                tree->clear();
                auto* topProf = new QTreeWidgetItem(tree);
                topProf->setText(0, QStringLiteral("Profiller"));
                auto* topFace = new QTreeWidgetItem(tree);
                topFace->setText(0, QStringLiteral("3DFACE"));
                auto* topNode = new QTreeWidgetItem(tree);
                topNode->setText(0, QStringLiteral("Noktalar"));
                auto* topLine = new QTreeWidgetItem(tree);
                topLine->setText(0, QStringLiteral("Çizgiler"));
                auto* topAsm = new QTreeWidgetItem(tree);
                topAsm->setText(0, QStringLiteral("Assembly'ler"));
                auto* topWeld = new QTreeWidgetItem(tree);
                topWeld->setText(0, QStringLiteral("Kaynaklar"));
                for (int i = 0; i < count; ++i) {
                    const QString cat = QString::fromStdString(app_.modelCategory(static_cast<std::size_t>(i)));
                    const QString name = QString::fromStdString(app_.modelDisplayName(static_cast<std::size_t>(i)));
                    if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)) continue;
                    QTreeWidgetItem* top =
                        (cat == QLatin1String("3DFACE")) ? topFace :
                        (cat == QLatin1String("NOKTA"))  ? topNode :
                        (cat == QLatin1String("PROFIL")) ? topProf : topLine;
                    auto* child = new QTreeWidgetItem(top);
                    child->setText(0, name);
                    child->setData(0, Qt::UserRole, i);
                    child->setData(0, Qt::UserRole + 1, QStringLiteral("part"));
                }
                // Assembly hiyerarsisi: Assembly A001 -> Ana parca + ikincil parcalar.
                const auto& asms = app_.assemblies();
                for (std::size_t ai = 0; ai < asms.size(); ++ai) {
                    const auto& a = asms[ai];
                    auto* asmNode = new QTreeWidgetItem(topAsm);
                    asmNode->setText(0, QString::fromStdWString(a.name));
                    asmNode->setData(0, Qt::UserRole, static_cast<qulonglong>(ai));
                    asmNode->setData(0, Qt::UserRole + 1, QStringLiteral("assembly"));
                    for (const mm::EntityId pid : a.parts) {
                        if (const auto mi = app_.document().indexOfEntity(pid)) {
                            auto* partNode = new QTreeWidgetItem(asmNode);
                            const bool isMain = (pid == a.mainPart);
                            partNode->setText(0, (isMain ? QStringLiteral("Ana: ") : QString()) +
                                QString::fromStdString(app_.modelDisplayName(*mi)));
                            partNode->setData(0, Qt::UserRole, static_cast<qulonglong>(*mi));
                            partNode->setData(0, Qt::UserRole + 1, QStringLiteral("part"));
                        }
                    }
                }
                // Kaynak dugumleri: W001 — Fillet 5mm Shop.
                const auto& welds = app_.welds();
                for (std::size_t wi = 0; wi < welds.size(); ++wi) {
                    const auto& w = welds[wi];
                    const QString typeName = (w.type == mm::WeldType::Fillet)
                        ? QStringLiteral("Fillet") : QStringLiteral("Butt");
                    const QString clsName = (w.weldClass == mm::WeldClass::Shop)
                        ? QStringLiteral("Shop") : QStringLiteral("Site");
                    auto* wNode = new QTreeWidgetItem(topWeld);
                    wNode->setText(0, QString::fromStdString(w.identification) +
                        QStringLiteral(" — ") + typeName + QLatin1Char(' ') +
                        QString::number(w.size) + QStringLiteral("mm ") + clsName);
                    wNode->setData(0, Qt::UserRole, static_cast<qulonglong>(wi));
                    wNode->setData(0, Qt::UserRole + 1, QStringLiteral("weld"));
                }
                topProf->setHidden(topProf->childCount() == 0);
                topFace->setHidden(topFace->childCount() == 0);
                topNode->setHidden(topNode->childCount() == 0);
                topLine->setHidden(topLine->childCount() == 0);
                topAsm->setHidden(topAsm->childCount() == 0);
                topWeld->setHidden(topWeld->childCount() == 0);
                tree->expandAll();
            }
            // Secili ogeleri kalin goster (her turda, ozyinelemeli).
            const auto& sel = app_.selectedModelIndices();
            const auto& wsel = app_.selectedWeldIndices();
            std::function<void(QTreeWidgetItem*)> applyBold = [&](QTreeWidgetItem* node) {
                const QString kind = node->data(0, Qt::UserRole + 1).toString();
                bool on = false;
                if (kind == QLatin1String("weld")) {
                    const qulonglong wi = node->data(0, Qt::UserRole).toULongLong();
                    on = std::find(wsel.begin(), wsel.end(),
                                   static_cast<std::size_t>(wi)) != wsel.end();
                } else if (kind == QLatin1String("part")) {
                    const qulonglong idx = node->data(0, Qt::UserRole).toULongLong();
                    on = std::find(sel.begin(), sel.end(),
                                   static_cast<std::size_t>(idx)) != sel.end();
                }
                bool anyChildBold = false;
                for (int c = 0; c < node->childCount(); ++c) {
                    applyBold(node->child(c));
                    anyChildBold = anyChildBold || node->child(c)->font(0).bold();
                }
                if (kind == QLatin1String("assembly")) on = anyChildBold;
                QFont f = node->font(0);
                f.setBold(on);
                node->setFont(0, f);
            };
            for (int t = 0; t < tree->topLevelItemCount(); ++t)
                applyBold(tree->topLevelItem(t));
        });
    treeTimer->start();

    QObject::connect(tree, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (!item) return;
            const QString kind = item->data(0, Qt::UserRole + 1).toString();
            if (kind == QLatin1String("assembly")) {
                const qulonglong ai = item->data(0, Qt::UserRole).toULongLong();
                const auto& asms = app_.assemblies();
                if (ai < asms.size()) {
                    std::vector<std::size_t> idxs;
                    for (const mm::EntityId pid : asms[ai].parts)
                        if (const auto mi = app_.document().indexOfEntity(pid)) idxs.push_back(*mi);
                    app_.selectModels(idxs);
                }
            } else if (kind == QLatin1String("weld")) {
                app_.selectWeld(static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong()));
            } else if (item->data(0, Qt::UserRole).isValid()) {
                app_.selectModel(static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong()));
            }
        });
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (!item) return;
            const QString kind = item->data(0, Qt::UserRole + 1).toString();
            if (kind == QLatin1String("weld")) {
                // Kaynak dugumune cift tik: kaynak ozelliklerini acip duzenle.
                const std::size_t wi = static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong());
                if (wi >= app_.welds().size()) return;
                const mm::Weld w = app_.welds()[wi];
                mm::WeldType type = w.type;
                double size = w.size, length = w.length;
                mm::WeldClass cls = w.weldClass;
                if (!openWeldDialog(type, size, length, cls)) return;
                app_.editWeld(wi, type, size, length, cls);
            } else if (item->data(0, Qt::UserRole).isValid()) {
                app_.focusModel(static_cast<std::size_t>(item->data(0, Qt::UserRole).toULongLong()));
            }
        });

    // Secim degisince ozellikleri tazele (500ms yoklama — Application
    // QObject degil, sinyal altyapisi yok).
    QTimer* refreshTimer = new QTimer(this);
    refreshTimer->setInterval(500);
    QObject::connect(refreshTimer, &QTimer::timeout, this, [this, propsProfile, propsLayer,
                                                           propsColor, propsLineType, propsMaterial,
                                                           propsRotation, propsColumnTop,
                                                           propsColumnBottom,
                                                           typeLabel, lengthValue, weldAsmInfo]() {
        static int lastIndex = -2;
        static QString lastProfile;
        static std::optional<std::size_t> lastWeld;
        static std::size_t lastSelSize = 0;
        const int index = app_.selectedModelIndex();
        const QString profile = QString::fromStdString(app_.selectedEntityProfile());
        const std::optional<std::size_t> weldIdx = app_.selectedWeldIndex();
        const std::size_t selSize = app_.selectedModelIndices().size();
        if (index == lastIndex && profile == lastProfile &&
            weldIdx == lastWeld && selSize == lastSelSize) return;
        lastIndex = index;
        lastProfile = profile;
        lastWeld = weldIdx;
        lastSelSize = selSize;

        const bool hasSelection = index >= 0;
        typeLabel->setText(hasSelection
            ? QString::fromStdString(app_.selectedEntityTypeLabel())
            : "Seçili obje yok");
        lengthValue->setText(hasSelection
            ? QString::fromStdString(app_.selectedEntityLengthLabel())
            : "-");

        // KAYNAK / ASSEMBLY bilgisi (Phase W2).
        QString asmInfo;
        if (weldIdx && *weldIdx < app_.welds().size()) {
            const mm::Weld& w = app_.welds()[*weldIdx];
            const QString tn = (w.type == mm::WeldType::Fillet)
                ? QStringLiteral("Fillet (Köşe)") : QStringLiteral("Butt (Alın)");
            const QString cn = (w.weldClass == mm::WeldClass::Shop)
                ? QStringLiteral("Shop (Atölye)") : QStringLiteral("Site (Şantiye)");
            asmInfo = QString::fromStdString(w.identification) + QStringLiteral(" — ") + tn +
                QStringLiteral("\nÖlçü: ") + QString::number(w.size) + QStringLiteral(" mm") +
                QStringLiteral("\nUzunluk: ") +
                (w.length > 0.0 ? QString::number(w.length) + QStringLiteral(" mm")
                                : QStringLiteral("otomatik")) +
                QStringLiteral("\nSınıf: ") + cn +
                QStringLiteral("\nGeometri: ") +
                (w.geometrySupported ? QStringLiteral("katı destekleniyor")
                                     : QStringLiteral("katı üretilmez (sembol)"));
        } else if (selSize > 0) {
            const auto& asms = app_.assemblies();
            const auto& sel = app_.selectedModelIndices();
            for (const auto& a : asms) {
                bool hit = false;
                for (const std::size_t idx : sel) {
                    if (idx >= app_.document().models().size()) continue;
                    const auto c = app_.document().assemblyIdContaining(app_.document().modelId(idx));
                    if (c && *c == a.id) { hit = true; break; }
                }
                if (!hit) continue;
                std::size_t weldCount = 0;
                for (const auto& w : app_.welds())
                    if (std::find(a.parts.begin(), a.parts.end(), w.mainPart) != a.parts.end() &&
                        std::find(a.parts.begin(), a.parts.end(), w.secondaryPart) != a.parts.end())
                        ++weldCount;
                QString mainName;
                if (const auto mi = app_.document().indexOfEntity(a.mainPart))
                    mainName = QString::fromStdString(app_.modelDisplayName(*mi));
                asmInfo = QString::fromStdWString(a.name) +
                    QStringLiteral("\nAna parça: ") + (mainName.isEmpty() ? QStringLiteral("-") : mainName) +
                    QStringLiteral("\nParça sayısı: ") + QString::number(static_cast<qulonglong>(a.parts.size())) +
                    QStringLiteral("\nKaynak sayısı: ") + QString::number(static_cast<qulonglong>(weldCount));
                break;
            }
        }
        weldAsmInfo->setText(asmInfo);
        weldAsmInfo->setVisible(!asmInfo.isEmpty());

        // Profil: liste BIR KEZ kuruldu (yer tutucu + katalog). Burada yalniz
        // gosterilecek deger secilir — secim varsa nesnenin profili, yoksa
        // AKTIF (bekleyen) profil; boylece kullanicinin sectigi deger kaybolmaz.
        QSignalBlocker profileBlocker(propsProfile);
        if (!profile.isEmpty()) {
            propsProfile->setCurrentText(profile);
        } else {
            const QString pending = QString::fromStdString(app_.pendingProfileName());
            if (pending.isEmpty()) propsProfile->setCurrentIndex(0); // profilsiz
            else propsProfile->setCurrentText(pending);
        }
        profileBlocker.unblock();

        // Katmanlar
        QSignalBlocker layerBlocker(propsLayer);
        propsLayer->clear();
        const auto layers = app_.layerNames();
        for (const auto& layer : layers)
            propsLayer->addItem(QString::fromStdString(layer));
        propsLayer->setCurrentText(QString::fromStdString(app_.selectedEntityLayer()));
        layerBlocker.unblock();

        // Renkler
        QSignalBlocker colorBlocker(propsColor);
        propsColor->clear();
        const auto& palette = Application::colorPalette();
        for (const auto& entry : palette) {
            const QString label = QString::fromWCharArray(entry.first);
            if (entry.second)
                propsColor->addItem(label);
            else
                propsColor->addItem(label + " (ByLayer)");
        }
        const int colorIndex = app_.selectedEntityColorIndex();
        if (colorIndex >= 0 && colorIndex < propsColor->count())
            propsColor->setCurrentIndex(colorIndex);
        colorBlocker.unblock();

        // Cizgi tipleri
        QSignalBlocker lineTypeBlocker(propsLineType);
        propsLineType->clear();
        for (const auto& choice : Application::lineTypePalette())
            propsLineType->addItem(QString::fromStdString(choice));
        const QString currentLineType =
            QString::fromStdString(app_.selectedEntityLineType());
        if (!currentLineType.isEmpty()) propsLineType->setCurrentText(currentLineType);
        lineTypeBlocker.unblock();

        // Malzeme
        QSignalBlocker materialBlocker(propsMaterial);
        const QString currentMaterial =
            QString::fromStdString(app_.selectedEntityMaterial());
        const QStringList materials = {"S235JR", "S275JR", "S355J2", "S450",
                                       "S690QL", "BETON", "DİĞER"};
        if (propsMaterial->count() == 0) propsMaterial->addItems(materials);
        if (!currentMaterial.isEmpty()) propsMaterial->setCurrentText(currentMaterial);
        materialBlocker.unblock();

        // Rotasyon
        QSignalBlocker rotationBlocker(propsRotation);
        propsRotation->setValue(app_.selectedEntityProfileRotation());
        rotationBlocker.unblock();

        // Kolon kotlari: secili DUSEY profilli elemanin GERCEK ust/alt kotunu
        // goster. Eskiden bu iki satir secimden hic okunmuyordu (hep 8500/0
        // kaliyordu) — "kolon boyunu degistirmiyor" sikayetinin bir parcasi.
        // Secili eleman kolon degilse (kiris vb.) mevcut deger korunur: bu
        // kutular ayni zamanda SONRAKI kolon yerlesiminin varsayilanidir.
        double selectionTop = 0.0, selectionBottom = 0.0;
        if (app_.selectedEntityColumnLevels(selectionTop, selectionBottom)) {
            QSignalBlocker columnTopBlocker(propsColumnTop);
            QSignalBlocker columnBottomBlocker(propsColumnBottom);
            propsColumnTop->setValue(selectionTop);
            propsColumnBottom->setValue(selectionBottom);
        }
    });
    refreshTimer->start();

    // Profil alani: secim YOKKEN cizilecek kirisin profilini belirler, secim
    // VARKEN profili secime atar. Liste secimi (textActivated) ve elle yazip
    // ENTER (returnPressed) AYNI yolu kullanir → satirda Enter = dogrudan uygula.
    const auto applyProfileField = [this, propsProfile]() {
        const std::string name = comboProfileName(propsProfile);
        app_.setPendingProfileName(name);            // cizilecek kirisin profili
        if (app_.hasSelection()) app_.assignProfileToSelection(name);
        // Odagi tuvale geri ver (eski ribbon combo'su da yapardi): yoksa
        // cizim kisayollari (L/M/D...) panele gider ve calismaz. Kuyruklanir ki
        // popup kapanisi ile cakismasin.
        QTimer::singleShot(0, this, [this]() {
            if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);
        });
    };
    QObject::connect(propsProfile, &QComboBox::textActivated, this,
        [applyProfileField](const QString&) { applyProfileField(); });
    if (QLineEdit* profileEdit = propsProfile->lineEdit())
        QObject::connect(profileEdit, &QLineEdit::returnPressed, this, applyProfileField);
    QObject::connect(propsLayer, &QComboBox::textActivated, this,
        [this](const QString& text) { app_.setCurrentLayer(text.toStdString()); });
    QObject::connect(propsColor, QOverload<int>::of(&QComboBox::activated), this,
        [this](int index) { app_.setCurrentColorChoice(index); });
    QObject::connect(propsLineType, QOverload<int>::of(&QComboBox::activated), this,
        [this](int index) { app_.setCurrentLineTypeChoice(index); });
    QObject::connect(propsMaterial, &QComboBox::textActivated, this,
        [this](const QString& text) {
            if (!text.trimmed().isEmpty())
                app_.setSelectedEntityMaterial(text.trimmed().toStdString());
        });
    // Malzeme: elle yazip ENTER -> ayni sekilde dogrudan uygulanir.
    if (QLineEdit* materialEdit = propsMaterial->lineEdit())
        QObject::connect(materialEdit, &QLineEdit::returnPressed, this,
            [this, propsMaterial]() {
                const QString text = propsMaterial->currentText().trimmed();
                if (!text.isEmpty())
                    app_.setSelectedEntityMaterial(text.toStdString());
            });
    // Rotasyon: satirda ENTER (veya odak cikisi) UYGULA gibi dogrudan uygular;
    // UYGULA butonu da korunur (ayni yolu kullanir).
    const auto applyRotationField = [this, propsRotation]() {
        if (app_.hasSelection())
            app_.setSelectedEntityProfileRotation(propsRotation->value());
    };
    QObject::connect(propsRotation, &QDoubleSpinBox::editingFinished, this,
                     applyRotationField);
    // Kolon ust/alt kot: satirda ENTER (editingFinished) degeri SONRAKI kolon
    // yerlesimi icin saklar VE secili kolona dogrudan uygular — tek yol
    // app_.applyColumnLevels(). valueChanged yalniz "bekleyen" degeri tutar.
    QObject::connect(propsColumnTop, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, propsMaterial](double v) {
            app_.setColumnProps(v, app_.columnBottomZ(),
                                propsMaterial->currentText().toStdString());
        });
    QObject::connect(propsColumnBottom, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this, propsMaterial](double v) {
            app_.setColumnProps(app_.columnTopZ(), v,
                                propsMaterial->currentText().toStdString());
        });
    const auto applyColumnFields = [this, propsColumnTop, propsColumnBottom]() {
        app_.applyColumnLevels(propsColumnTop->value(), propsColumnBottom->value());
    };
    QObject::connect(propsColumnTop, &QDoubleSpinBox::editingFinished, this, applyColumnFields);
    QObject::connect(propsColumnBottom, &QDoubleSpinBox::editingFinished, this, applyColumnFields);
    // UYGULA: panelde dogrudan uygulanabilir satirlarin TAMAMI (rotasyon +
    // kolon kotlari). Profil/katman/renk/cizgi tipi/malzeme secimde aninda
    // uygulanir; kolon kotlari ve rotasyon UYGULA/ENTER ile uygulanir.
    QObject::connect(propsApply, &QPushButton::clicked, this,
        [applyRotationField, applyColumnFields]() {
            applyRotationField();
            applyColumnFields();
        });
    // Kolon yerlestir: profesyonel bir sekilde — buton kolon modunu etkinlestirir
    // (tek tik ile kanvasa kolon koymak icin). Profil col-inbox'tan okunur.
    QObject::connect(propsColumnBtn, &QPushButton::clicked, this,
        [this, propsProfile, propsMaterial]() {
            const std::string profile = comboProfileName(propsProfile);
            app_.setPendingProfileName(profile); // bos = profilsiz kolon
            app_.setColumnProps(app_.columnTopZ(), app_.columnBottomZ(),
                                propsMaterial->currentText().toStdString());
            app_.selectTool(DrawTool::Column);
        });

    // ---- ALT SECIM CUBUGU (Tekla tarzi): secili nesne sayisi + snap/ortho ----
    QLabel* selCount = new QLabel(QStringLiteral("0 nesne seçildi"), this);
    selCount->setStyleSheet("color: #C8CCD4; padding: 0 8px;");
    QLabel* selState = new QLabel(QStringLiteral("Snap: —"), this);
    selState->setStyleSheet("color: #C8CCD4; padding: 0 8px;");
    statusBar()->addPermanentWidget(selCount);
    statusBar()->addPermanentWidget(selState);
    QTimer* selBarTimer = new QTimer(this);
    selBarTimer->setInterval(500);
    QObject::connect(selBarTimer, &QTimer::timeout, this, [this, selCount, selState]() {
        const std::size_t n = app_.selectedModelIndices().size();
        selCount->setText(n == 0 ? QStringLiteral("0 nesne seçildi")
                                 : (n == 1 ? QStringLiteral("1 nesne seçildi")
                                           : QStringLiteral("%1 nesne seçildi")
                                                 .arg(static_cast<qulonglong>(n))));
        selState->setText(QStringLiteral("Snap: %1 · Ortho: %2")
            .arg(app_.snapEnabled() ? QStringLiteral("Açık") : QStringLiteral("Kapalı"))
            .arg(app_.orthoEnabled() ? QStringLiteral("Açık") : QStringLiteral("Kapalı")));
    });
    selBarTimer->start();
}

} // namespace mm
