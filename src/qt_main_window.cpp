#include "model_maker/qt_main_window.hpp"
#include "model_maker/drafting.hpp"

#include <QApplication>
#include <QMenu>
#include <QToolButton>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPushButton>
#include <QComboBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QTimer>
#include <QMessageBox>
#include <QLabel>
#include <QWindow>
#include <QResizeEvent>

#include <algorithm>
#include <fstream>
#include <string>

#include <windows.h>

namespace mm {

QtMainWindow::QtMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , app_(GetModuleHandleW(nullptr))
{
    setWindowTitle("Model Maker — Professional Wireframe CAD");
    resize(1400, 900);

    createMenus();
    createToolbar();
    createDockPanels();

    // Create a plain widget for central area — fills all space between docks
    canvasContainer_ = new QWidget(this);
    setCentralWidget(canvasContainer_);

    // Create Application's window, then embed its canvas
    app_.createMainWindow(SW_HIDE);

    HWND canvas = app_.canvasHandle();
    HWND appWnd = app_.windowHandle();

    // Reparent canvas to our widget
    canvasContainer_->winId(); // force native handle
    HWND qtHwnd = reinterpret_cast<HWND>(canvasContainer_->winId());
    SetParent(canvas, qtHwnd);
    ShowWindow(appWnd, SW_HIDE);

    RECT rc;
    GetClientRect(qtHwnd, &rc);
    SetWindowPos(canvas, nullptr, 0, 0, rc.right, rc.bottom,
                 SWP_NOZORDER | SWP_SHOWWINDOW);

    statusBar()->showMessage("Hazır");
}

QtMainWindow::~QtMainWindow() = default;

void QtMainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
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
    fileMenu->addAction("&Yeni", this, [this]() { app_.selectTool(DrawTool::Line); });
    fileMenu->addAction("DXF &İçe Aktar...", this, []() {});
    fileMenu->addAction("DXF &Dışa Aktar...", this, []() {});
    fileMenu->addSeparator();
    fileMenu->addAction("Çı&kış", qApp, &QApplication::quit);

    QMenu* drawMenu = menuBar()->addMenu("Çi&zim");
    drawMenu->addAction("Çiz&gi", this, [this]() { app_.selectTool(DrawTool::Line); });
    drawMenu->addAction("&Polyline", this, [this]() { app_.selectTool(DrawTool::Polyline); });
    drawMenu->addAction("Dikdört&gen", this, [this]() { app_.selectTool(DrawTool::Rectangle); });
    drawMenu->addAction("Dai&re", this, [this]() { app_.selectTool(DrawTool::Circle); });
    drawMenu->addAction("&3DFACE", this, [this]() { app_.selectTool(DrawTool::Face3D); });

    QMenu* modifyMenu = menuBar()->addMenu("Dü&zenle");
    modifyMenu->addAction("&Pasif", this, [this]() { app_.deactivateAllCommands(); });
    modifyMenu->addSeparator();
    modifyMenu->addAction("&Taşı", this, [this]() { app_.startTransformCommand(TransformCommand::Move); });
    modifyMenu->addAction("&Kopyala", this, [this]() { app_.startTransformCommand(TransformCommand::Copy); });
    modifyMenu->addAction("&Ofset", this, [this]() { app_.startTransformCommand(TransformCommand::Offset); });
    modifyMenu->addAction("A&yna", this, [this]() { app_.startTransformCommand(TransformCommand::Mirror); });
    modifyMenu->addAction("&Sil", this, [this]() { app_.startTransformCommand(TransformCommand::Delete); });
    modifyMenu->addSeparator();
    modifyMenu->addAction("Doğrusal &Dizi", this, [this]() { app_.startTransformCommand(TransformCommand::LinearArray); });
    modifyMenu->addAction("Dairesel Di&zi", this, [this]() { app_.startTransformCommand(TransformCommand::PolarArray); });
    modifyMenu->addSeparator();
    modifyMenu->addAction("&Trim", this, [this]() { app_.startTransformCommand(TransformCommand::Trim); });
    modifyMenu->addAction("E&xtend", this, [this]() { app_.startTransformCommand(TransformCommand::Extend); });
    modifyMenu->addAction("&Fillet", this, [this]() { app_.startTransformCommand(TransformCommand::Fillet); });

    QMenu* viewMenu = menuBar()->addMenu("G&örünüm");
    viewMenu->addAction("&Zoom Extents", this, [this]() { app_.zoomExtents2D(); });
    viewMenu->addAction("Zoom &Window", this, [this]() { app_.startZoomWindow2D(); });
    viewMenu->addAction("2&B / 3B", this, [this]() { app_.toggle3DView(); });
}

void QtMainWindow::createToolbar() {
    QToolBar* ribbon = addToolBar("Ribbon");
    ribbon->setMovable(false);

    QTabWidget* tabs = new QTabWidget(ribbon);
    tabs->setDocumentMode(true);
    tabs->setTabPosition(QTabWidget::North);
    tabs->setMaximumWidth(620);

    // ---- Çizim tab ----
    QWidget* drawTab = new QWidget();
    QHBoxLayout* drawLayout = new QHBoxLayout(drawTab);
    drawLayout->setContentsMargins(4, 2, 4, 2);
    drawLayout->setSpacing(2);
    auto addDrawBtn = [&](const QString& text, DrawTool tool) {
        QPushButton* btn = new QPushButton(text, drawTab);
        btn->setFixedSize(55, 36);
        btn->setStyleSheet("QPushButton { font-size: 9pt; }");
        QObject::connect(btn, &QPushButton::clicked, this, [this, tool]() { app_.selectTool(tool); });
        drawLayout->addWidget(btn);
    };
    addDrawBtn("╱ Çizgi", DrawTool::Line);
    addDrawBtn("⌁ PL", DrawTool::Polyline);
    addDrawBtn("□ Rect", DrawTool::Rectangle);
    addDrawBtn("○ Daire", DrawTool::Circle);
    addDrawBtn("▱ 3DF", DrawTool::Face3D);
    drawLayout->addStretch();
    tabs->addTab(drawTab, "Çizim");

    // ---- Düzenle tab ----
    QWidget* modTab = new QWidget();
    QHBoxLayout* modLayout = new QHBoxLayout(modTab);
    modLayout->setContentsMargins(4, 2, 4, 2);
    modLayout->setSpacing(2);
    auto addModBtn = [&](const QString& text, auto action) {
        QPushButton* btn = new QPushButton(text, modTab);
        btn->setFixedSize(55, 36);
        btn->setStyleSheet("QPushButton { font-size: 9pt; }");
        QObject::connect(btn, &QPushButton::clicked, this, action);
        modLayout->addWidget(btn);
    };
    addModBtn("↖ Pasif",  [this]() { app_.deactivateAllCommands(); });
    addModBtn("↔ Taşı",   [this]() { app_.startTransformCommand(TransformCommand::Move); });
    addModBtn("⧉ Kopya",  [this]() { app_.startTransformCommand(TransformCommand::Copy); });
    addModBtn("⇶ Ofset",  [this]() { app_.startTransformCommand(TransformCommand::Offset); });
    addModBtn("◁▷ Ayna", [this]() { app_.startTransformCommand(TransformCommand::Mirror); });
    addModBtn("✕ Sil",    [this]() { app_.startTransformCommand(TransformCommand::Delete); });
    addModBtn("▦ Dizi",  [this]() { app_.startTransformCommand(TransformCommand::LinearArray); });
    addModBtn("◌ Daire", [this]() { app_.startTransformCommand(TransformCommand::PolarArray); });
    addModBtn("✂ Trim",   [this]() { app_.startTransformCommand(TransformCommand::Trim); });
    addModBtn("↗ Extend", [this]() { app_.startTransformCommand(TransformCommand::Extend); });
    addModBtn("⌒ Fillet", [this]() { app_.startTransformCommand(TransformCommand::Fillet); });
    modLayout->addStretch();
    tabs->addTab(modTab, "Düzenle");

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
}

void QtMainWindow::createDockPanels() {
    QDockWidget* propsDock = new QDockWidget("Özellikler", this);
    propsDock->setWidget(new QWidget(propsDock));
    propsDock->setMinimumWidth(250);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);
}

} // namespace mm
