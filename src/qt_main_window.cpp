#include "model_maker/qt_main_window.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/two_point_view.hpp"

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
#include <QMenu>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFont>
#include <functional>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QTimer>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QMessageBox>
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
            app_.setSelectedEndPlateConnection(p);
#endif
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

void QtMainWindow::createMenus() {
    // Tekla tarzi: ust menubar yerine sol ust kosedeki TEK "Menu" dugmesi.
    // Tum menuler bu tek kok menuye (appMenu_) baglanir; dugme createToolbar'da
    // ribbon'un en soluna InstantPopup olarak eklenir. Menubar gizlenir.
    menuBar()->setVisible(false);
    appMenu_ = new QMenu(this);
    appMenu_->setTitle(QStringLiteral("Menü"));

    QMenu* fileMenu = appMenu_->addMenu("&Dosya");
    fileMenu->addAction("&Yeni", this, [this]() { app_.newDocument(); })->setIcon(makeToolIcon(ToolGlyph::New));
    fileMenu->addAction("&Aç...", this, [this]() { app_.openDocument(); })->setIcon(makeToolIcon(ToolGlyph::Open));
    fileMenu->addAction("&Kaydet", this, [this]() { app_.saveDocument(); })->setIcon(makeToolIcon(ToolGlyph::Save));
    fileMenu->addAction("Farklı Kay&det...", this, [this]() { app_.saveDocumentAs(); });
    fileMenu->addSeparator();
    fileMenu->addAction("DXF &İçe Aktar...", this, [this]() { app_.importDxf(); })->setIcon(makeToolIcon(ToolGlyph::Import));
    fileMenu->addAction("DXF &Dışa Aktar...", this, [this]() { app_.exportDxf(); })->setIcon(makeToolIcon(ToolGlyph::Export));
    fileMenu->addSeparator();
    fileMenu->addAction("Çı&kış", qApp, &QApplication::quit);

    QMenu* drawMenu = appMenu_->addMenu("Çi&zim");
    drawMenu->addAction("Çiz&gi", this, [this]() { app_.selectTool(DrawTool::Line); })->setIcon(makeToolIcon(ToolGlyph::Line));
    drawMenu->addAction("&Polyline", this, [this]() { app_.selectTool(DrawTool::Polyline); })->setIcon(makeToolIcon(ToolGlyph::Polyline));
    drawMenu->addAction("Dikdört&gen", this, [this]() { app_.selectTool(DrawTool::Rectangle); })->setIcon(makeToolIcon(ToolGlyph::Rect));
    drawMenu->addAction("Dai&re", this, [this]() { app_.selectTool(DrawTool::Circle); })->setIcon(makeToolIcon(ToolGlyph::Circle));
    drawMenu->addAction("Böl (&Divide)", this, [this]() { app_.activateDivide(); });
    drawMenu->addAction("&3DFACE", this, [this]() { app_.selectTool(DrawTool::Face3D); })->setIcon(makeToolIcon(ToolGlyph::Face3D));

    QMenu* modifyMenu = appMenu_->addMenu("Dü&zenle");
    modifyMenu->addAction("&Pasif", this, [this]() { app_.deactivateAllCommands(); })->setIcon(makeToolIcon(ToolGlyph::Pan));
    modifyMenu->addSeparator();
    modifyMenu->addAction("&Taşı", this, [this]() { app_.startTransformCommand(TransformCommand::Move); })->setIcon(makeToolIcon(ToolGlyph::Move));
    modifyMenu->addAction("&Kopyala", this, [this]() { app_.startTransformCommand(TransformCommand::Copy); })->setIcon(makeToolIcon(ToolGlyph::Copy));
    modifyMenu->addAction("&Ofset", this, [this]() { app_.startTransformCommand(TransformCommand::Offset); })->setIcon(makeToolIcon(ToolGlyph::Offset));
    modifyMenu->addAction("A&yna", this, [this]() { app_.startTransformCommand(TransformCommand::Mirror); })->setIcon(makeToolIcon(ToolGlyph::Mirror));
    modifyMenu->addAction("&Sil", this, [this]() { app_.startTransformCommand(TransformCommand::Delete); })->setIcon(makeToolIcon(ToolGlyph::Delete));
    modifyMenu->addSeparator();
    modifyMenu->addAction("Doğrusal &Dizi", this, [this]() { app_.startTransformCommand(TransformCommand::LinearArray); })->setIcon(makeToolIcon(ToolGlyph::LinearArray));
    modifyMenu->addAction("Dairesel Di&zi", this, [this]() { app_.startTransformCommand(TransformCommand::PolarArray); })->setIcon(makeToolIcon(ToolGlyph::PolarArray));
    modifyMenu->addSeparator();
    modifyMenu->addAction("&Trim", this, [this]() { app_.startTransformCommand(TransformCommand::Trim); })->setIcon(makeToolIcon(ToolGlyph::Trim));
    modifyMenu->addAction("E&xtend", this, [this]() { app_.startTransformCommand(TransformCommand::Extend); })->setIcon(makeToolIcon(ToolGlyph::Extend));
    modifyMenu->addAction("&Fillet", this, [this]() { app_.startTransformCommand(TransformCommand::Fillet); })->setIcon(makeToolIcon(ToolGlyph::Fillet));

    // ---- KONSTRÜKSİYON & KESME: konstrüksiyon nesneleri + kesme komutları ----
    QMenu* constructionMenu = appMenu_->addMenu("K&onstrüksiyon");
    constructionMenu->addAction("Konstrüksiyon &Düzlemi (3 Nokta)", this,
        [this]() { app_.startConstructionPlaneCommand(); })->setIcon(makeToolIcon(ToolGlyph::Plane));
    constructionMenu->addAction("Konstrüksiyon &Nesnelerini Göster/Gizle", this,
        [this]() { app_.toggleConstructionVisibility(); });
    constructionMenu->addSeparator();
    constructionMenu->addAction("&Fitting (Uç Kesme)", this,
        [this]() { app_.startFittingCommand(); });
    constructionMenu->addAction("&Plane Cut", this,
        [this]() { app_.startPlaneCutCommand(); });
    constructionMenu->addAction("&Line Cut (Çizgi)", this,
        [this]() { app_.startLineCutCommand(); });
    constructionMenu->addAction("&Polygon Cut (3DFACE)", this,
        [this]() { app_.startPolygonCutCommand(); });
    constructionMenu->addAction("&Part Cut (Parça)", this,
        [this]() { app_.startPartCutCommand(); });
    constructionMenu->addSeparator();
    constructionMenu->addAction("&Bolt (Bulon)", this,
        [this]() { app_.startBoltCommand(); })->setIcon(makeToolIcon(ToolGlyph::Bolt));
    constructionMenu->addAction("&End Plate (Alın Levhası)", this,
        [this]() {
            mm::EndPlateParams p = app_.pendingEndPlateParams();
            if (!openEndPlateParamsDialog(p, p)) return;
            app_.setPendingEndPlateParams(p);
            app_.startEndPlateCommand();
        })->setIcon(makeToolIcon(ToolGlyph::Plate));

    // ---- GÖRÜNÜM: kamera/navigasyon + standart yönler ----
    QMenu* viewMenu = appMenu_->addMenu("G&örünüm");
    viewMenu->addAction("&Zoom Extents", this, [this]() { app_.zoomExtents2D(); })->setIcon(makeToolIcon(ToolGlyph::Extents));
    viewMenu->addAction("Zoom &Window", this, [this]() { app_.startZoomWindow2D(); })->setIcon(makeToolIcon(ToolGlyph::Extents));
    viewMenu->addAction("2&B / 3B", this, [this]() { app_.toggle3DView(); })->setIcon(makeToolIcon(ToolGlyph::Toggle3D));
    viewMenu->addSeparator();
    QMenu* viewDirMenu = viewMenu->addMenu("Görü&nüş");
    viewDirMenu->addAction("Ön&den", this, [this]() { app_.setStandardView(StandardView::Front); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    viewDirMenu->addAction("Ar&kadan", this, [this]() { app_.setStandardView(StandardView::Back); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    viewDirMenu->addAction("&Sol", this, [this]() { app_.setStandardView(StandardView::Left); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    viewDirMenu->addAction("&Sağ", this, [this]() { app_.setStandardView(StandardView::Right); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
    viewDirMenu->addSeparator();
    viewDirMenu->addAction("Üs&t", this, [this]() { app_.setStandardView(StandardView::Top); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));
    viewDirMenu->addAction("Al&t", this, [this]() { app_.setStandardView(StandardView::Bottom); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+6")));
    viewDirMenu->addSeparator();
    viewDirMenu->addAction("&İzometrik", this, [this]() { app_.setStandardView(StandardView::Isometric); })
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+7")));
    viewMenu->addSeparator();
    viewMenu->addAction("Görünümü Sıfırla (Ctrl+R)", this,
        [this]() { app_.resetView(); })->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    viewMenu->addAction("&Döndürme Merkezi (R)", this, [this]() { app_.startRotateCenterCommand(); });
    viewMenu->addAction("Döndürme Merkezini &Temizle", this, [this]() { app_.clearRotateCenter(); });
    viewMenu->addSeparator();
    viewMenu->addAction("&Benchmark (GDI vs GL)", this, [this]() {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "MENU-BENCH-CLICKED\n"); fclose(diag); }
        app_.runRenderBenchmark();
    });

    // ---- ÇALIŞMA DÜZLEMİ & UCS: work plane + UCS + adlandırılmış görünümler ----
    QMenu* workPlaneMenu = appMenu_->addMenu("Çalışma &Düzlemi");
    workPlaneMenu->addAction("Çalışma &Düzlemi (3 Nokta)", this, [this]() { app_.startWorkPlaneCommand(); })->setIcon(makeToolIcon(ToolGlyph::Plane));
    workPlaneMenu->addAction("Düzlemi &Sıfırla (Dünya)", this, [this]() { app_.resetWorkPlane(); })->setIcon(makeToolIcon(ToolGlyph::Reset));
    QAction* ghostPlaneAction = workPlaneMenu->addAction("Çalışma Düzlemi &Hayaleti (F7)", this,
        [this]() { app_.toggleWorkPlaneGhost(); });
    ghostPlaneAction->setCheckable(true);
    ghostPlaneAction->setChecked(app_.workPlaneGhostVisible());
    workPlaneMenu->addSeparator();
    workPlaneMenu->addAction("UCS'ye &Dik Görünüş", this, [this]() { app_.setViewToWorkPlane(); })
        ->setIcon(makeToolIcon(ToolGlyph::Plane));
    QMenu* globalPlaneMenu = workPlaneMenu->addMenu("Çalışma Düzlemi (&Global)");
    globalPlaneMenu->addAction("&XY (Üstten)", this, [this]() { app_.setGlobalWorkPlane(0); });
    globalPlaneMenu->addAction("&XZ (Önden)", this, [this]() { app_.setGlobalWorkPlane(1); });
    globalPlaneMenu->addAction("&YZ (Sağdan)", this, [this]() { app_.setGlobalWorkPlane(2); });
    workPlaneMenu->addSeparator();
    workPlaneMenu->addAction("Görünümü &Adlandır...", this, [this]() { promptSaveNamedView(); });
    workPlaneMenu->addAction("Adlandırılmış &Görünüme Git...", this, [this]() { promptGotoNamedView(); });
    workPlaneMenu->addAction("Adlandırılmış Görünümü &Sil...", this, [this]() { promptDeleteNamedView(); });

    // ---- GÖRÜNÜŞLER: yeni görünüş pencereleri (MDI) ----
    QMenu* viewsMenu = appMenu_->addMenu("Görünüş&ler");
    QAction* twoPointAction = viewsMenu->addAction("&2 Noktalı Görünüş (XY'ye dik)", this,
        [this]() { app_.startTwoPointViewCommand(); });
    twoPointAction->setIcon(makeToolIcon(ToolGlyph::Plane));
    twoPointAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    QAction* planViewAction = viewsMenu->addAction("&Plan Görünüşü (Z kotu...)", this,
        [this]() { app_.startPlanViewCommand(); });
    planViewAction->setIcon(makeToolIcon(ToolGlyph::Extents));
    planViewAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    QMenu* newViewMenu = viewsMenu->addMenu("&Yeni Görünüş");
    newViewMenu->addAction("&3 Noktalı Görünüş", this,
        [this]() { app_.startThreePointViewCommand(); });
    newViewMenu->addAction("&Çalışma Düzleminde Görünüş", this,
        [this]() { app_.startViewOnWorkPlaneCommand(); });
    newViewMenu->addSeparator();
    newViewMenu->addAction("Parça &Ön Düzlemi", this,
        [this]() { app_.startPartPlaneViewCommand(PartPlane::Front); });
    newViewMenu->addAction("Parça &Üst Düzlemi", this,
        [this]() { app_.startPartPlaneViewCommand(PartPlane::Top); });
    newViewMenu->addAction("Parça &Uç Düzlemi", this,
        [this]() { app_.startPartPlaneViewCommand(PartPlane::End); });
    newViewMenu->addAction("Parça &Arka Düzlemi", this,
        [this]() { app_.startPartPlaneViewCommand(PartPlane::Back); });
    newViewMenu->addAction("Parça &Alt Düzlemi", this,
        [this]() { app_.startPartPlaneViewCommand(PartPlane::Bottom); });
    newViewMenu->addSeparator();
    newViewMenu->addAction("Parçanın &Varsayılan Görünüşleri (Ön/Üst/Uç)", this,
        [this]() { app_.startPartDefaultViewsCommand(); });
    newViewMenu->addAction("Parçanın &3B Görünüşü", this,
        [this]() { app_.startPart3DViewCommand(); });
    newViewMenu->addSeparator();
    newViewMenu->addAction("Grid &Çizgileri Boyunca Görünüşler", this,
        [this]() { app_.startGridViewsCommand(); });

    // ---- GRID ----
    QMenu* gridMenu = appMenu_->addMenu("&Grid");
    gridMenu->addAction("&Yapı Gridi Oluştur...", this, [this]() { promptCreateGrid(); });
    gridMenu->addAction("&Yapı Gridleri...", this, [this]() { promptGridManager(); });

    QMenu* windowMenu = appMenu_->addMenu("&Pencere");
    windowMenu->addAction("&Döşe", this, [this]() { if (mdiArea_) mdiArea_->tileSubWindows(); });
    windowMenu->addAction("&Basamakla", this, [this]() { if (mdiArea_) mdiArea_->cascadeSubWindows(); });
    windowMenu->addAction("&Model görünüşünü büyüt", this, [this]() {
        if (modelSubWindow_) { modelSubWindow_->showMaximized(); mdiArea_->setActiveSubWindow(modelSubWindow_); }
    });
    windowMenu->addSeparator();
    windowMenu->addAction("Tüm görünüşleri &kapat", this, [this]() { closeAllTwoPointViews(); });
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

    const double effTop = (current.topExtra >= 0.0) ? current.topExtra : current.plateExtra;
    const double effBot = (current.bottomExtra >= 0.0) ? current.bottomExtra : current.plateExtra;

    auto* topExtraSpin = new QDoubleSpinBox(picPage);
    topExtraSpin->setRange(0.0, 300.0);
    topExtraSpin->setDecimals(1);
    topExtraSpin->setValue(effTop);
    topExtraSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Üst kenar payı (kesit üstü → levha üstü):"), topExtraSpin);

    auto* botExtraSpin = new QDoubleSpinBox(picPage);
    botExtraSpin->setRange(0.0, 300.0);
    botExtraSpin->setDecimals(1);
    botExtraSpin->setValue(effBot);
    botExtraSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Alt kenar payı (kesit altı → levha altı):"), botExtraSpin);

    auto* gapSpin = new QDoubleSpinBox(picPage);
    gapSpin->setRange(0.0, 100.0);
    gapSpin->setDecimals(1);
    gapSpin->setValue(current.gap);
    gapSpin->setSuffix(QStringLiteral(" mm"));
    picForm->addRow(QStringLiteral("Kolon boşluğu (levha ↔ ana parça):"), gapSpin);

    auto* picNote = new QLabel(picPage);
    picNote->setWordWrap(true);
    picNote->setStyleSheet(QStringLiteral("color: gray;"));
    picNote->setText(QStringLiteral(
        "Levha kirişin gerçek kesitine ortalanır. Üst/alt pay ayrı ayrı ayarlanır; "
        "üst pay > alt pay ise levha yukarı kayar (Tekla 144 Resim sekmesi karşılığı)."));
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
    out.topExtra = topExtraSpin->value();
    out.bottomExtra = botExtraSpin->value();
    out.gap = gapSpin->value();
    out.plateThickness = thickSpin->value();
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

    // ---- Çizim tab ----
    QWidget* drawTab = new QWidget();
    QHBoxLayout* drawLayout = new QHBoxLayout(drawTab);
    drawLayout->setContentsMargins(4, 2, 4, 2);
    drawLayout->setSpacing(2);
    auto addDrawBtn = [&](const ToolGlyph glyph, const QString& text, DrawTool tool) {
        QToolButton* btn = new QToolButton(drawTab);
        btn->setIcon(makeToolIcon(glyph));
        btn->setIconSize(QSize(22, 22));
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(58, 48);
        btn->setStyleSheet("QToolButton { font-size: 7.5pt; color: #E8E8E8; border: none; border-radius: 3px; background: transparent; }" "QToolButton:hover { background: #3C3C44; }" "QToolButton:pressed { background: #23232B; }");
        QObject::connect(btn, &QToolButton::clicked, this, [this, tool]() { app_.selectTool(tool); });
        drawLayout->addWidget(btn);
    };
    addDrawBtn(ToolGlyph::Line, "Çizgi", DrawTool::Line);
    addDrawBtn(ToolGlyph::Polyline, "Polyline", DrawTool::Polyline);
    addDrawBtn(ToolGlyph::Rect, "Dikdörtgen", DrawTool::Rectangle);
    addDrawBtn(ToolGlyph::Circle, "Daire", DrawTool::Circle);
    addDrawBtn(ToolGlyph::Face3D, "3DFACE", DrawTool::Face3D);
    drawLayout->addStretch();
    tabs->addTab(drawTab, "Çizim");

    // ---- Düzenle tab ----
    QWidget* modTab = new QWidget();
    QHBoxLayout* modLayout = new QHBoxLayout(modTab);
    modLayout->setContentsMargins(4, 2, 4, 2);
    modLayout->setSpacing(2);
    auto addModBtn = [&](const ToolGlyph glyph, const QString& text, auto action) {
        QToolButton* btn = new QToolButton(modTab);
        btn->setIcon(makeToolIcon(glyph));
        btn->setIconSize(QSize(20, 20));
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(52, 48);
        btn->setStyleSheet("QToolButton { font-size: 7.5pt; color: #E8E8E8; border: none; border-radius: 3px; background: transparent; }" "QToolButton:hover { background: #3C3C44; }" "QToolButton:pressed { background: #23232B; }");
        QObject::connect(btn, &QToolButton::clicked, this, action);
        modLayout->addWidget(btn);
    };
    addModBtn(ToolGlyph::Undo,  "Geri",   [this]() { app_.undo(); });
    addModBtn(ToolGlyph::Redo,  "İleri",  [this]() { app_.redo(); });
    addModBtn(ToolGlyph::Pan,   "Pasif",  [this]() { app_.deactivateAllCommands(); });
    addModBtn(ToolGlyph::Move,  "Taşı",   [this]() { app_.startTransformCommand(TransformCommand::Move); });
    addModBtn(ToolGlyph::Copy,  "Kopyala",[this]() { app_.startTransformCommand(TransformCommand::Copy); });
    addModBtn(ToolGlyph::Offset,"Ofset",  [this]() { app_.startTransformCommand(TransformCommand::Offset); });
    addModBtn(ToolGlyph::Mirror,"Ayna",   [this]() { app_.startTransformCommand(TransformCommand::Mirror); });
    addModBtn(ToolGlyph::Delete,"Sil",    [this]() { app_.startTransformCommand(TransformCommand::Delete); });
    addModBtn(ToolGlyph::LinearArray, "Dizi",   [this]() { app_.startTransformCommand(TransformCommand::LinearArray); });
    addModBtn(ToolGlyph::PolarArray,  "D.Dizi", [this]() { app_.startTransformCommand(TransformCommand::PolarArray); });
    addModBtn(ToolGlyph::Trim,   "Trim",   [this]() { app_.startTransformCommand(TransformCommand::Trim); });
    addModBtn(ToolGlyph::Extend, "Extend", [this]() { app_.startTransformCommand(TransformCommand::Extend); });
    addModBtn(ToolGlyph::Fillet, "Fillet", [this]() { app_.startTransformCommand(TransformCommand::Fillet); });
    modLayout->addStretch();
    tabs->addTab(modTab, "Düzenle");

    // ---- Ortak sekme yardimcilari: her sekme ayni stil/davranisla kurulur ----
    const QString tabBtnStyle = QStringLiteral(
        "QToolButton { font-size: 7.5pt; color: #E8E8E8; border: none; border-radius: 3px; background: transparent; }"
        "QToolButton:hover { background: #3C3C44; }"
        "QToolButton:pressed { background: #23232B; }"
        "QToolButton::menu-indicator { image: none; }");
    auto beginTab = [&](const QString& title) {
        QWidget* w = new QWidget();
        QHBoxLayout* l = new QHBoxLayout(w);
        l->setContentsMargins(4, 2, 4, 2);
        l->setSpacing(2);
        tabs->addTab(w, title);
        return l;
    };
    auto addBtn = [&](QHBoxLayout* lay, const ToolGlyph glyph, const QString& text,
                      const QString& tip, auto action) {
        QToolButton* btn = new QToolButton();
        btn->setIcon(makeToolIcon(glyph));
        btn->setIconSize(QSize(22, 22));
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(58, 48);
        btn->setToolTip(tip);
        btn->setStyleSheet(tabBtnStyle);
        QObject::connect(btn, &QToolButton::clicked, this, action);
        lay->addWidget(btn);
    };
    auto addSep = [&](QHBoxLayout* lay) {
        QFrame* line = new QFrame();
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Plain);
        line->setStyleSheet(QStringLiteral("color: #3C3C44;"));
        lay->addWidget(line);
    };
    auto addDrop = [&](QHBoxLayout* lay, const ToolGlyph glyph, const QString& text,
                       const QString& tip, auto fill) {
        QToolButton* btn = new QToolButton();
        btn->setIcon(makeToolIcon(glyph));
        btn->setIconSize(QSize(22, 22));
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(58, 48);
        btn->setToolTip(tip);
        btn->setStyleSheet(tabBtnStyle);
        btn->setPopupMode(QToolButton::InstantPopup);
        QMenu* menu = new QMenu(btn);
        fill(menu);
        btn->setMenu(menu);
        lay->addWidget(btn);
    };

    // ---- Konstrüksiyon sekmesi: konstrüksiyon nesneleri + kesme komutlari ----
    {
        QHBoxLayout* lay = beginTab("Konstrüksiyon");
        addBtn(lay, ToolGlyph::Plane,    "K. Düzlem",     "Konstrüksiyon düzlemi (3 nokta)", [this]() { app_.startConstructionPlaneCommand(); });
        addBtn(lay, ToolGlyph::Toggle3D, "Gizle/Göster",  "Konstrüksiyon nesnelerini göster/gizle", [this]() { app_.toggleConstructionVisibility(); });
        addSep(lay);
        addBtn(lay, ToolGlyph::Trim,     "Fitting",       "Fitting (uç kesme)", [this]() { app_.startFittingCommand(); });
        addBtn(lay, ToolGlyph::Plane,    "Plane Cut",     "Düzlemle kesme", [this]() { app_.startPlaneCutCommand(); });
        addBtn(lay, ToolGlyph::Line,     "Line Cut",      "Çizgiyle kesme", [this]() { app_.startLineCutCommand(); });
        addBtn(lay, ToolGlyph::Face3D,   "Polygon Cut",   "3DFACE ile kesme", [this]() { app_.startPolygonCutCommand(); });
        addBtn(lay, ToolGlyph::Delete,   "Part Cut",      "Parça ile kesme", [this]() { app_.startPartCutCommand(); });
        addSep(lay);
        addBtn(lay, ToolGlyph::Bolt,     "Bolt",          "İki parçayı bulonla birleştir", [this]() { app_.startBoltCommand(); });
        addBtn(lay, ToolGlyph::Plate,    "End Plate",     "Alın levhası bağlantısı (kolon+kiriş)", [this]() {
            mm::EndPlateParams p = app_.pendingEndPlateParams();
            if (!openEndPlateParamsDialog(p, p)) return;
            app_.setPendingEndPlateParams(p);
            app_.startEndPlateCommand();
        });
        lay->addStretch();
    }

    // ---- Görünüm sekmesi: kamera/navigasyon + standart yönler ----
    {
        QHBoxLayout* lay = beginTab("Görünüm");
        addBtn(lay, ToolGlyph::Toggle3D, "2B/3B",    "2B / 3B görünüm", [this]() { app_.toggle3DView(); });
        addBtn(lay, ToolGlyph::Extents,  "Extents",  "Zoom Extents", [this]() { app_.zoomExtents2D(); });
        addBtn(lay, ToolGlyph::Extents,  "Zoom Penc.","Zoom Window", [this]() { app_.startZoomWindow2D(); });
        addBtn(lay, ToolGlyph::Reset,    "Sıfırla",  "Görünümü sıfırla (Ctrl+R)", [this]() { app_.resetView(); });
        addSep(lay);
        addDrop(lay, ToolGlyph::Extents, "Görünüş", "Standart görünüş yönleri (Ctrl+1..7)", [this](QMenu* m) {
            m->addAction(QStringLiteral("Önden (Ctrl+1)"),  this, [this]() { app_.setStandardView(StandardView::Front); });
            m->addAction(QStringLiteral("Arkadan (Ctrl+2)"), this, [this]() { app_.setStandardView(StandardView::Back); });
            m->addAction(QStringLiteral("Soldan (Ctrl+3)"),  this, [this]() { app_.setStandardView(StandardView::Left); });
            m->addAction(QStringLiteral("Sağdan (Ctrl+4)"),  this, [this]() { app_.setStandardView(StandardView::Right); });
            m->addSeparator();
            m->addAction(QStringLiteral("Üstten (Ctrl+5)"),  this, [this]() { app_.setStandardView(StandardView::Top); });
            m->addAction(QStringLiteral("Alttan (Ctrl+6)"),  this, [this]() { app_.setStandardView(StandardView::Bottom); });
            m->addSeparator();
            m->addAction(QStringLiteral("İzometrik (Ctrl+7)"), this, [this]() { app_.setStandardView(StandardView::Isometric); });
        });
        addBtn(lay, ToolGlyph::Toggle3D, "Dön. Mrk.", "Döndürme merkezi seç (R)", [this]() { app_.startRotateCenterCommand(); });
        addBtn(lay, ToolGlyph::Reset,    "Mrk. Sil",  "Döndürme merkezini temizle", [this]() { app_.clearRotateCenter(); });
        addSep(lay);
        addBtn(lay, ToolGlyph::Extents,  "Benchmark", "GDI vs GL render benchmark", [this]() { app_.runRenderBenchmark(); });
        lay->addStretch();
    }

    // ---- Çalışma Düzlemi sekmesi: work plane + UCS + adlandırılmış görünümler ----
    {
        QHBoxLayout* lay = beginTab("Çalışma Düzlemi");
        addBtn(lay, ToolGlyph::Plane,    "Düzlem",     "Çalışma düzlemi (3 nokta)", [this]() { app_.startWorkPlaneCommand(); });
        addBtn(lay, ToolGlyph::Reset,    "Düz. Sıfır", "Düzlemi sıfırla (dünya)", [this]() { app_.resetWorkPlane(); });
        addBtn(lay, ToolGlyph::Toggle3D, "Hayalet",    "Çalışma düzlemi hayaleti (F7)", [this]() { app_.toggleWorkPlaneGhost(); });
        addSep(lay);
        addBtn(lay, ToolGlyph::Plane,    "UCS Dik",    "UCS'ye dik görünüş", [this]() { app_.setViewToWorkPlane(); });
        addDrop(lay, ToolGlyph::Plane,   "Global",     "Çalışma düzlemi (global)", [this](QMenu* m) {
            m->addAction(QStringLiteral("XY (Üstten)"), this, [this]() { app_.setGlobalWorkPlane(0); });
            m->addAction(QStringLiteral("XZ (Önden)"),  this, [this]() { app_.setGlobalWorkPlane(1); });
            m->addAction(QStringLiteral("YZ (Sağdan)"), this, [this]() { app_.setGlobalWorkPlane(2); });
        });
        addSep(lay);
        addBtn(lay, ToolGlyph::Save,   "Adlandır", "Görünümü adlandır...", [this]() { promptSaveNamedView(); });
        addBtn(lay, ToolGlyph::Open,   "Gör. Git", "Adlandırılmış görünüme git...", [this]() { promptGotoNamedView(); });
        addBtn(lay, ToolGlyph::Delete, "Gör. Sil", "Adlandırılmış görünümü sil...", [this]() { promptDeleteNamedView(); });
        lay->addStretch();
    }

    // ---- Görünüşler sekmesi: yeni görünüş pencereleri (MDI) ----
    {
        QHBoxLayout* lay = beginTab("Görünüşler");
        addBtn(lay, ToolGlyph::Plane,   "2N Gör.",  "2 noktalı görünüş — XY'ye dik (Ctrl+Shift+V)", [this]() { app_.startTwoPointViewCommand(); });
        addBtn(lay, ToolGlyph::Extents, "Plan Gör.", "Plan görünüşü — Z kotu ile (Ctrl+Shift+P)", [this]() { app_.startPlanViewCommand(); });
        addDrop(lay, ToolGlyph::Plane,  "Yeni Görünüş", "Tekla tarzı yeni görünüş komutları", [this](QMenu* m) {
            m->addAction(QStringLiteral("3 Noktalı Görünüş"), this, [this]() { app_.startThreePointViewCommand(); });
            m->addAction(QStringLiteral("Çalışma Düzleminde Görünüş"), this, [this]() { app_.startViewOnWorkPlaneCommand(); });
            m->addSeparator();
            m->addAction(QStringLiteral("Parça Ön Düzlemi"), this, [this]() { app_.startPartPlaneViewCommand(PartPlane::Front); });
            m->addAction(QStringLiteral("Parça Üst Düzlemi"), this, [this]() { app_.startPartPlaneViewCommand(PartPlane::Top); });
            m->addAction(QStringLiteral("Parça Uç Düzlemi"), this, [this]() { app_.startPartPlaneViewCommand(PartPlane::End); });
            m->addAction(QStringLiteral("Parça Arka Düzlemi"), this, [this]() { app_.startPartPlaneViewCommand(PartPlane::Back); });
            m->addAction(QStringLiteral("Parça Alt Düzlemi"), this, [this]() { app_.startPartPlaneViewCommand(PartPlane::Bottom); });
            m->addSeparator();
            m->addAction(QStringLiteral("Parçanın Varsayılan Görünüşleri (Ön/Üst/Uç)"), this, [this]() { app_.startPartDefaultViewsCommand(); });
            m->addAction(QStringLiteral("Parçanın 3B Görünüşü"), this, [this]() { app_.startPart3DViewCommand(); });
            m->addSeparator();
            m->addAction(QStringLiteral("Grid Çizgileri Boyunca Görünüşler"), this, [this]() { app_.startGridViewsCommand(); });
        });
        lay->addStretch();
    }

    // ---- Grid sekmesi: yapı gridi ----
    {
        QHBoxLayout* lay = beginTab("Grid");
        addBtn(lay, ToolGlyph::Extents, "Grid Oluştur", "Yapı gridi oluştur...", [this]() { promptCreateGrid(); });
        addBtn(lay, ToolGlyph::Extents, "Gridler",      "Yapı gridleri...", [this]() { promptGridManager(); });
        lay->addStretch();
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
    struct QuickCmd { QString label; std::function<void()> run; };
    std::vector<QuickCmd> cmds = {
        {QStringLiteral("Çizgi"),          [this]() { app_.selectTool(DrawTool::Line); }},
        {QStringLiteral("Kiriş"),          [this]() { app_.selectTool(DrawTool::Line); }},
        {QStringLiteral("Dikdörtgen"),     [this]() { app_.selectTool(DrawTool::Rectangle); }},
        {QStringLiteral("Daire"),          [this]() { app_.selectTool(DrawTool::Circle); }},
        {QStringLiteral("3DFACE"),         [this]() { app_.selectTool(DrawTool::Face3D); }},
        {QStringLiteral("Taşı"),           [this]() { app_.startTransformCommand(TransformCommand::Move); }},
        {QStringLiteral("Kopyala"),        [this]() { app_.startTransformCommand(TransformCommand::Copy); }},
        {QStringLiteral("Ayna"),           [this]() { app_.startTransformCommand(TransformCommand::Mirror); }},
        {QStringLiteral("Sil"),            [this]() { app_.startTransformCommand(TransformCommand::Delete); }},
        {QStringLiteral("Fitting"),        [this]() { app_.startFittingCommand(); }},
        {QStringLiteral("Plane Cut"),      [this]() { app_.startPlaneCutCommand(); }},
        {QStringLiteral("Line Cut"),       [this]() { app_.startLineCutCommand(); }},
        {QStringLiteral("Polygon Cut"),    [this]() { app_.startPolygonCutCommand(); }},
        {QStringLiteral("Part Cut"),       [this]() { app_.startPartCutCommand(); }},
        {QStringLiteral("Yapı Gridi"),     [this]() { promptCreateGrid(); }},
        {QStringLiteral("2 Noktalı Görünüş"), [this]() { app_.startTwoPointViewCommand(); }},
        {QStringLiteral("İzometrik"),      [this]() { app_.setStandardView(StandardView::Isometric); }},
        {QStringLiteral("Ön Görünüş"),     [this]() { app_.setStandardView(StandardView::Front); }},
        {QStringLiteral("Odaklan"),        [this]() { app_.focusOnSelection(); }},
    };
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
            static QString lastFilter;
            const int count = app_.modelCount();
            const QString filter = treeSearch->text().trimmed();
            if (count != lastCount || filter != lastFilter) {
                lastCount = count;
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
                }
                topProf->setHidden(topProf->childCount() == 0);
                topFace->setHidden(topFace->childCount() == 0);
                topNode->setHidden(topNode->childCount() == 0);
                topLine->setHidden(topLine->childCount() == 0);
                tree->expandAll();
            }
            // Secili ogeleri kalin goster (her turda).
            const auto& sel = app_.selectedModelIndices();
            for (int t = 0; t < tree->topLevelItemCount(); ++t) {
                QTreeWidgetItem* top = tree->topLevelItem(t);
                for (int c = 0; c < top->childCount(); ++c) {
                    QTreeWidgetItem* child = top->child(c);
                    const int idx = child->data(0, Qt::UserRole).toInt();
                    const bool isSel = std::find(sel.begin(), sel.end(),
                        static_cast<std::size_t>(idx)) != sel.end();
                    QFont f = child->font(0);
                    f.setBold(isSel);
                    child->setFont(0, f);
                }
            }
        });
    treeTimer->start();

    QObject::connect(tree, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (!item) return;
            const QVariant d = item->data(0, Qt::UserRole);
            if (!d.isValid()) return;
            app_.selectModel(static_cast<std::size_t>(d.toInt()));
        });
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (!item) return;
            const QVariant d = item->data(0, Qt::UserRole);
            if (!d.isValid()) return;
            app_.focusModel(static_cast<std::size_t>(d.toInt()));
        });

    // Secim degisince ozellikleri tazele (500ms yoklama — Application
    // QObject degil, sinyal altyapisi yok).
    QTimer* refreshTimer = new QTimer(this);
    refreshTimer->setInterval(500);
    QObject::connect(refreshTimer, &QTimer::timeout, this, [this, propsProfile, propsLayer,
                                                           propsColor, propsLineType, propsMaterial,
                                                           propsRotation, propsColumnTop,
                                                           propsColumnBottom,
                                                           typeLabel, lengthValue]() {
        static int lastIndex = -2;
        static QString lastProfile;
        const int index = app_.selectedModelIndex();
        const QString profile = QString::fromStdString(app_.selectedEntityProfile());
        if (index == lastIndex && profile == lastProfile) return;
        lastIndex = index;
        lastProfile = profile;

        const bool hasSelection = index >= 0;
        typeLabel->setText(hasSelection
            ? QString::fromStdString(app_.selectedEntityTypeLabel())
            : "Seçili obje yok");
        lengthValue->setText(hasSelection
            ? QString::fromStdString(app_.selectedEntityLengthLabel())
            : "-");

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
