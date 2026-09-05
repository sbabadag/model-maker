#include "model_maker/qt_main_window.hpp"
#include "model_maker/drafting.hpp"

#include <QApplication>
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
#include <QPushButton>
#include <QComboBox>
#include <QCompleter>
#include <QColorDialog>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QTimer>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
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
    Toggle3D, Extents, Plane, Reset, New, Open, Save, Import, Export
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

namespace mm {

QtMainWindow::QtMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , app_(GetModuleHandleW(nullptr))
{
    setWindowTitle(QString("Model Maker — Professional Wireframe CAD  [%1 %2]")
                       .arg(__DATE__)
                       .arg(__TIME__));
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

    // Create a plain widget for central area — fills all space between docks
    canvasContainer_ = new QWidget(this);
    setCentralWidget(canvasContainer_);
    canvasContainer_->installEventFilter(this);

    // Create Application's window, then embed its canvas
    app_.createMainWindow(SW_HIDE);

    HWND canvas = app_.canvasHandle();
    HWND appWnd = app_.windowHandle();

    // Reparent canvas to our widget
    canvasContainer_->winId(); // force native handle
    HWND qtHwnd = reinterpret_cast<HWND>(canvasContainer_->winId());
    SetParent(canvas, qtHwnd);
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

QtMainWindow::~QtMainWindow() = default;

void QtMainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    resizeEmbeddedCanvas();
}

void QtMainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // The central widget only reaches its final size during the first layout
    // pass after the window is shown; without this the embedded GDI canvas
    // keeps the tiny pre-layout rect from the constructor (top-left corner).
    QTimer::singleShot(0, this, [this]() { resizeEmbeddedCanvas(); });
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
    if (watched == canvasContainer_ && event->type() == QEvent::Resize)
        resizeEmbeddedCanvas();
    return QMainWindow::eventFilter(watched, event);
}

void QtMainWindow::resizeEmbeddedCanvas() {
    HWND canvas = app_.canvasHandle();
    if (canvas && canvasContainer_) {
        RECT rc;
        GetClientRect(reinterpret_cast<HWND>(canvasContainer_->winId()), &rc);
        SetWindowPos(canvas, nullptr, 0, 0, rc.right, rc.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void QtMainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu("&Dosya");
    fileMenu->addAction("&Yeni", this, [this]() { app_.newDocument(); })->setIcon(makeToolIcon(ToolGlyph::New));
    fileMenu->addAction("&Aç...", this, [this]() { app_.openDocument(); })->setIcon(makeToolIcon(ToolGlyph::Open));
    fileMenu->addAction("&Kaydet", this, [this]() { app_.saveDocument(); })->setIcon(makeToolIcon(ToolGlyph::Save));
    fileMenu->addSeparator();
    fileMenu->addAction("DXF &İçe Aktar...", this, [this]() { app_.importDxf(); })->setIcon(makeToolIcon(ToolGlyph::Import));
    fileMenu->addAction("DXF &Dışa Aktar...", this, [this]() { app_.exportDxf(); })->setIcon(makeToolIcon(ToolGlyph::Export));
    fileMenu->addAction("DXF &İçe Aktar...", this, []() {});
    fileMenu->addAction("DXF &Dışa Aktar...", this, []() {});
    fileMenu->addSeparator();
    fileMenu->addAction("Çı&kış", qApp, &QApplication::quit);

    QMenu* drawMenu = menuBar()->addMenu("Çi&zim");
    drawMenu->addAction("Çiz&gi", this, [this]() { app_.selectTool(DrawTool::Line); })->setIcon(makeToolIcon(ToolGlyph::Line));
    drawMenu->addAction("&Polyline", this, [this]() { app_.selectTool(DrawTool::Polyline); })->setIcon(makeToolIcon(ToolGlyph::Polyline));
    drawMenu->addAction("Dikdört&gen", this, [this]() { app_.selectTool(DrawTool::Rectangle); })->setIcon(makeToolIcon(ToolGlyph::Rect));
    drawMenu->addAction("Dai&re", this, [this]() { app_.selectTool(DrawTool::Circle); })->setIcon(makeToolIcon(ToolGlyph::Circle));
    drawMenu->addAction("&3DFACE", this, [this]() { app_.selectTool(DrawTool::Face3D); })->setIcon(makeToolIcon(ToolGlyph::Face3D));

    QMenu* modifyMenu = menuBar()->addMenu("Dü&zenle");
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

    QMenu* viewMenu = menuBar()->addMenu("G&örünüm");
    viewMenu->addAction("&Zoom Extents", this, [this]() { app_.zoomExtents2D(); })->setIcon(makeToolIcon(ToolGlyph::Extents));
    viewMenu->addAction("Zoom &Window", this, [this]() { app_.startZoomWindow2D(); })->setIcon(makeToolIcon(ToolGlyph::Extents));
    viewMenu->addAction("2&B / 3B", this, [this]() { app_.toggle3DView(); })->setIcon(makeToolIcon(ToolGlyph::Toggle3D));
    viewMenu->addSeparator();
    viewMenu->addAction("Çalışma &Düzlemi (3 Nokta)", this, [this]() { app_.startWorkPlaneCommand(); })->setIcon(makeToolIcon(ToolGlyph::Plane));
    viewMenu->addAction("Düzlemi &Sıfırla (Dünya)", this, [this]() { app_.resetWorkPlane(); })->setIcon(makeToolIcon(ToolGlyph::Reset));
    viewMenu->addSeparator();
    viewMenu->addAction("&Benchmark (GDI vs GL)", this, [this]() {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) { fprintf(diag, "MENU-BENCH-CLICKED\n"); fclose(diag); }
        app_.runRenderBenchmark();
    });
}

void QtMainWindow::createToolbar() {
    QToolBar* ribbon = addToolBar("Ribbon");
    ribbon->setMovable(false);
    ribbon->setStyleSheet(
        "QToolBar { background: #2B2B30; border: none; spacing: 2px; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #1E1E24; color: #B8B8C0; padding: 4px 12px; }"
        "QTabBar::tab:selected { background: #2B2B30; color: #FFFFFF; }");

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

    // ---- Görünüm tab (UCS / çalışma düzlemi) ----
    QWidget* viewTab = new QWidget();
    QHBoxLayout* viewLayout = new QHBoxLayout(viewTab);
    viewLayout->setContentsMargins(4, 2, 4, 2);
    viewLayout->setSpacing(2);
    auto addViewBtn = [&](const ToolGlyph glyph, const QString& text, auto action) {
        QToolButton* btn = new QToolButton(viewTab);
        btn->setIcon(makeToolIcon(glyph));
        btn->setIconSize(QSize(22, 22));
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setFixedSize(58, 48);
        btn->setStyleSheet("QToolButton { font-size: 7.5pt; color: #E8E8E8; border: none; border-radius: 3px; background: transparent; }" "QToolButton:hover { background: #3C3C44; }" "QToolButton:pressed { background: #23232B; }");
        QObject::connect(btn, &QToolButton::clicked, this, action);
        viewLayout->addWidget(btn);
    };
    addViewBtn(ToolGlyph::Toggle3D, "2B/3B",   [this]() { app_.toggle3DView(); });
    addViewBtn(ToolGlyph::Extents,  "Extents", [this]() { app_.zoomExtents2D(); });
    addViewBtn(ToolGlyph::Plane,    "Düzlem",  [this]() { app_.startWorkPlaneCommand(); });
    addViewBtn(ToolGlyph::Reset,    "Sıfırla", [this]() { app_.resetWorkPlane(); });
    viewLayout->addStretch();
    tabs->addTab(viewTab, "Görünüm");

    ribbon->addWidget(tabs);

    // ---- Persistent controls (right side, always visible) ----
    // Spacer to push everything right
    QWidget* spacer = new QWidget(ribbon);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ribbon->addWidget(spacer);

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

    // The Win32 window created by Application is intentionally hidden and
    // only its canvas is reparented into this QMainWindow.  Consequently any
    // controls parented to that Win32 host can never be part of the visible
    // UI.  Keep profile selection in a dedicated Qt toolbar row so it remains
    // visible on every ribbon tab and cannot overflow behind the tab widget.
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar* profileBar = new QToolBar("Çelik Profil", this);
    profileBar->setObjectName("profileToolbar");
    profileBar->setMovable(false);
    profileBar->setFloatable(false);
    profileBar->setAllowedAreas(Qt::TopToolBarArea);
    profileBar->setStyleSheet(
        "QToolBar { background: #34343B; border-top: 1px solid #4B4B55; "
        "border-bottom: 1px solid #1C1C22; spacing: 8px; padding: 3px 8px; }"
        "QLabel { color: #F0F0F4; font-weight: 600; }");
    addToolBar(Qt::TopToolBarArea, profileBar);

    QLabel* profileLabel = new QLabel("Çelik profil:", profileBar);
    profileLabel->setObjectName("profileSelectorLabel");
    profileBar->addWidget(profileLabel);

    profileSelector_ = new QComboBox(profileBar);
    profileSelector_->setObjectName("profileSelector");
    profileSelector_->setAccessibleName("Çelik profil seçici");
    profileSelector_->setEditable(true);
    profileSelector_->setInsertPolicy(QComboBox::NoInsert);
    profileSelector_->setMinimumWidth(280);
    profileSelector_->setMaximumWidth(420);
    profileSelector_->setMinimumContentsLength(24);
    profileSelector_->setMaxVisibleItems(22);
    profileSelector_->setToolTip(
        "Ctrl+tıklama ile çizgileri seçin; ardından atanacak Tekla profilini seçin (X ile açılır).");

    const auto profileNames = app_.profileNames();
    profileLabel->setText(QString("Çelik profil (%1):").arg(
        static_cast<int>(profileNames.size())));
    const auto upperName = [](std::string text) {
        for (auto& character : text)
            character = static_cast<char>(
                ::toupper(static_cast<unsigned char>(character)));
        return text;
    };
    const auto categoryOf = [&upperName](const std::string& name) -> const char* {
        const std::string u = upperName(name);
        if (u.rfind("HE", 0) == 0 || u.rfind("HL", 0) == 0 || u.rfind("HD", 0) == 0 ||
            u.rfind("HP", 0) == 0 || u.rfind("UB", 0) == 0 || u.rfind("UC", 0) == 0)
            return "I Kirişleri (HEA/HEB/HEM/HL/UB...)";
        if (u.rfind("IPE", 0) == 0 || u.rfind("INP", 0) == 0) return "IPE Profiller";
        if (u.rfind("KKR", 0) == 0 || u.rfind("SHS", 0) == 0 || u.rfind("RHS", 0) == 0)
            return "Kutu Profiller (KKR/SHS/RHS)";
        if (u.rfind("CFCHS", 0) == 0 || u.rfind("CHS", 0) == 0 || u.rfind("ROD", 0) == 0 ||
            u.rfind("PHI", 0) == 0 || u.rfind("PIPE", 0) == 0)
            return "Boru Profiller (CHS/CFCHS/PHI)";
        if (u.rfind("UNP", 0) == 0 || u.rfind("UPN", 0) == 0) return "U Kanallar (UNP/UPN)";
        if (u.rfind("PL", 0) == 0 || u.rfind("FL", 0) == 0) return "Lamalar (PL/FL)";
        if (u.rfind("L", 0) == 0) return "Köşebentler (L)";
        if (u.rfind("T", 0) == 0) return "T Profiller";
        return "Diğer";
    };
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& name : profileNames)
        groups[categoryOf(name)].push_back(name);
    for (const auto& entry : groups) {
        profileSelector_->insertSeparator(profileSelector_->count());
        profileSelector_->addItem(QString::fromStdString(entry.first));
        if (auto* standardModel =
                qobject_cast<QStandardItemModel*>(profileSelector_->model())) {
            QStandardItem* header =
                standardModel->item(profileSelector_->count() - 1);
            header->setEnabled(false);
            header->setBackground(QBrush(QColor(58, 69, 85)));
            header->setForeground(QBrush(QColor(240, 240, 244)));
        }
        for (const auto& name : entry.second)
            profileSelector_->addItem(QString::fromStdString(name));
    }
    if (profileNames.empty()) {
        profileSelector_->addItem("Profil kataloğu bulunamadı");
        profileSelector_->setEnabled(false);
    } else {
        for (int index = 0; index < profileSelector_->count(); ++index) {
            const QModelIndex modelIndex = profileSelector_->model()->index(index, 0);
            if (profileSelector_->model()->flags(modelIndex) & Qt::ItemIsEnabled) {
                profileSelector_->setCurrentIndex(index);
                break;
            }
        }
        if (QCompleter* completer = profileSelector_->completer()) {
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
            completer->setCompletionMode(QCompleter::PopupCompletion);
        }
    }
    profileBar->addWidget(profileSelector_);

    QLabel* profileHint = new QLabel("Ctrl+tıkla seç  →  profili seç", profileBar);
    profileHint->setObjectName("profileSelectorHint");
    profileHint->setStyleSheet("color: #C8CCD4; font-weight: 400;");
    profileBar->addWidget(profileHint);

    QObject::connect(profileSelector_, &QComboBox::textActivated, this,
        [this](const QString& text) {
            const QString name = text.trimmed();
            if (name.isEmpty()) return;
            const QByteArray utf8 = name.toUtf8();
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) {
                fprintf(diag, "PROFILE-QT-SELECT name=%s\n", utf8.constData());
                fclose(diag);
            }
            app_.assignProfileToSelection(utf8.toStdString());
            if (HWND canvas = app_.canvasHandle()) SetFocus(canvas);
        });
    app_.setProfilePickerCallback([this]() {
        if (!profileSelector_ || !profileSelector_->isEnabled()) return;
        // Queue out of the native canvas WndProc before entering a Qt popup.
        QTimer::singleShot(0, profileSelector_, [this]() {
            if (!profileSelector_) return;
            profileSelector_->setFocus(Qt::ShortcutFocusReason);
            profileSelector_->showPopup();
        });
    });

    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "PROFILE-QT-CREATED items=%d object=profileSelector\n",
                    profileSelector_->count());
            fclose(diag);
        }
    }
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

    // Profil
    QLabel* profileCaption = new QLabel("Profil", propsWidget);
    profileCaption->setStyleSheet("color: #C8CCD4; font-size: 8pt;");
    propsLayout->addWidget(profileCaption);
    QComboBox* propsProfile = new QComboBox(propsWidget);
    propsProfile->setObjectName("propsProfile");
    propsProfile->setEditable(true);
    propsProfile->setInsertPolicy(QComboBox::NoInsert);
    propsProfile->setMinimumContentsLength(18);
    if (QCompleter* completer = propsProfile->completer()) {
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
    }
    propsLayout->addWidget(propsProfile);

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
    propsLayout->addStretch(1);
    propsDock->setWidget(propsWidget);
    propsDock->setMinimumWidth(250);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    // Secim degisince ozellikleri tazele (500ms yoklama — Application
    // QObject degil, sinyal altyapisi yok).
    QTimer* refreshTimer = new QTimer(this);
    refreshTimer->setInterval(500);
    QObject::connect(refreshTimer, &QTimer::timeout, this, [this, propsProfile, propsLayer,
                                                           propsColor, propsLineType, propsMaterial,
                                                           propsRotation, typeLabel, lengthValue]() {
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

        // Profil: katalog + mevcut deger
        QSignalBlocker profileBlocker(propsProfile);
        propsProfile->clear();
        const auto names = app_.profileNames();
        for (const auto& name : names)
            propsProfile->addItem(QString::fromStdString(name));
        if (!profile.isEmpty()) propsProfile->setCurrentText(profile);
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
    });
    refreshTimer->start();

    // Profil degisince atama (secilene uygulanir)
    QObject::connect(propsProfile, &QComboBox::textActivated, this,
        [this](const QString& text) {
            if (!text.trimmed().isEmpty())
                app_.assignProfileToSelection(text.trimmed().toStdString());
        });
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
    // Rotasyon canli uygulanmaz — UYGULA ile uygulanir (Tekla tarzi);
    // panelden cikinca eski duruma donme sorununu da ortadan kaldirir.
    QObject::connect(propsApply, &QPushButton::clicked, this,
        [this, propsRotation]() {
            app_.setSelectedEntityProfileRotation(propsRotation->value());
        });
}

} // namespace mm
