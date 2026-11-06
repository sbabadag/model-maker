#include "model_maker/ribbon_widget.hpp"

// SERIT widget'i — Qt6, platformdan bagimsiz (windows.h YOK).
// Icerik tamamen CommandCatalog'tan gelir; burada elle komut/dugme yok.

#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mm {
namespace {

constexpr int kIconPx = 20;
constexpr int kLargeButtonHeight = 58;
constexpr int kSmallButtonHeight = 22;
constexpr int kButtonPadding = 14;

QString toQ(const std::string& text) { return QString::fromUtf8(text.c_str()); }

// Dosya adi icin sade slug (Turkce karakterler ASCII'ye iner).
QString slug(const QString& text) {
    QString out;
    for (QChar c : text) {
        const char16_t u = c.unicode();
        if (u < 128 && (std::isalnum(static_cast<int>(u)) != 0)) {
            out += QChar(static_cast<char16_t>(std::tolower(static_cast<int>(u))));
        } else if (u == 0x131 || u == 0x130 || u == 0x49 || u == 0x69) {
            out += QLatin1Char('i');
        } else if (u == 0x15F || u == 0x15E) {
            out += QLatin1Char('s');
        } else if (u == 0x11F || u == 0x11E) {
            out += QLatin1Char('g');
        } else if (u == 0xE7 || u == 0xC7) {
            out += QLatin1Char('c');
        } else if (u == 0xFC || u == 0xDC) {
            out += QLatin1Char('u');
        } else if (u == 0xF6 || u == 0xD6) {
            out += QLatin1Char('o');
        } else {
            out += QLatin1Char('-');
        }
    }
    while (out.contains(QStringLiteral("--"))) out.replace(QStringLiteral("--"), QStringLiteral("-"));
    if (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("tab") : out;
}

} // namespace

// ---------------------------------------------------------------------------
// IKONLAR — bizim cizim (tek renk, vektorel). Marka gorseli/resmi YOK.
// ---------------------------------------------------------------------------
QIcon commandIcon(const std::string& id, const QColor& color) {
    const double dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pix(static_cast<int>(kIconPx * dpr), static_cast<int>(kIconPx * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const double s = kIconPx;
    const auto line = [&](double x1, double y1, double x2, double y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto rect = [&](double x, double y, double w, double h) {
        p.drawRect(QRectF(x, y, w, h));
    };
    const auto arrow = [&](QPointF from, QPointF to) {
        line(from.x(), from.y(), to.x(), to.y());
        const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
        const double len = 3.4;
        for (double d : {angle + 2.6, angle - 2.6}) {
            line(to.x(), to.y(), to.x() + len * std::cos(d), to.y() + len * std::sin(d));
        }
    };

    if (id == "new") {
        p.drawPolyline(QPolygonF({QPointF(5, 3), QPointF(12, 3), QPointF(15, 6), QPointF(15, 17),
                                  QPointF(5, 17), QPointF(5, 3)}));
        line(10, 8, 10, 13);
        line(7.5, 10.5, 12.5, 10.5);
    } else if (id == "open") {
        p.drawPolyline(QPolygonF({QPointF(3, 16), QPointF(3, 6), QPointF(9, 6), QPointF(11, 8),
                                  QPointF(17, 8), QPointF(17, 16), QPointF(3, 16)}));
    } else if (id == "save" || id == "saveAs") {
        rect(4, 4, 12, 13);
        rect(7, 4, 6, 5);
        rect(7, 12, 6, 5);
    } else if (id == "import") {
        rect(3, 12, 14, 5);
        arrow(QPointF(10, 3), QPointF(10, 10));
    } else if (id == "export") {
        rect(3, 12, 14, 5);
        arrow(QPointF(10, 10), QPointF(10, 3));
    } else if (id == "exit") {
        p.drawPolyline(QPolygonF({QPointF(11, 4), QPointF(4, 4), QPointF(4, 16),
                                  QPointF(11, 16)}));
        arrow(QPointF(8, 10), QPointF(16, 10));
    } else if (id == "line") {
        line(4, 16, 16, 4);
        p.drawEllipse(QPointF(4, 16), 1.2, 1.2);
        p.drawEllipse(QPointF(16, 4), 1.2, 1.2);
    } else if (id == "polyline") {
        p.drawPolyline(QPolygonF({QPointF(3, 15), QPointF(7, 7), QPointF(11, 13),
                                  QPointF(17, 5)}));
    } else if (id == "rect") {
        rect(4, 6, 12, 8);
    } else if (id == "circle") {
        p.drawEllipse(QPointF(10, 10), 6, 6);
    } else if (id == "face3d") {
        p.drawPolygon(QPolygonF({QPointF(4, 13), QPointF(9, 5), QPointF(17, 5),
                                 QPointF(12, 13)}));
    } else if (id == "column") {
        line(7, 3, 7, 17);
        line(13, 3, 13, 17);
        line(7, 3, 13, 3);
        line(7, 17, 13, 17);
    } else if (id == "cube") {
        rect(5, 7, 9, 9);
        p.drawPolyline(QPolygonF({QPointF(5, 7), QPointF(8, 4), QPointF(17, 4)}));
        line(14, 7, 17, 4);
        line(14, 16, 17, 13);
        line(17, 4, 17, 13);
    } else if (id == "pyramid") {
        p.drawPolygon(QPolygonF({QPointF(10, 3), QPointF(17, 16), QPointF(3, 16)}));
        line(10, 3, 10, 16);
    } else if (id == "cylinder") {
        p.drawEllipse(QPointF(10, 5), 5, 2.2);
        line(5, 5, 5, 15);
        line(15, 5, 15, 15);
        p.drawArc(QRectF(5, 12.8, 10, 4.4), 180 * 16, 180 * 16);
    } else if (id == "divide") {
        line(3, 10, 17, 10);
        for (double x : {6.0, 10.0, 14.0}) {
            p.setBrush(color);
            p.drawEllipse(QPointF(x, 10), 1.3, 1.3);
            p.setBrush(Qt::NoBrush);
        }
    } else if (id == "paper") {
        rect(4, 3, 12, 14);
        line(7, 6, 13, 6);
        line(7, 9, 13, 9);
        line(7, 12, 11, 12);
    } else if (id == "focus") {
        p.drawEllipse(QPointF(9, 9), 5, 5);
        line(13, 13, 17, 17);
    } else if (id == "profile") {
        line(6, 4, 6, 16);
        line(14, 4, 14, 16);
        line(6, 4, 14, 4);
        line(6, 16, 14, 16);
        line(6, 10, 14, 10);
    } else if (id == "undo" || id == "redo") {
        const bool redo = id == "redo";
        p.drawArc(QRectF(4, 5, 12, 10), redo ? 30 * 16 : 150 * 16, 150 * 16);
        const QPointF tip = redo ? QPointF(16, 6) : QPointF(4, 6);
        line(tip.x(), tip.y(), tip.x() + (redo ? -3 : 3), tip.y());
        line(tip.x(), tip.y(), tip.x(), tip.y() + 3);
    } else if (id == "cancel") {
        p.drawEllipse(QPointF(10, 10), 6, 6);
        line(6, 14, 14, 6);
    } else if (id == "move") {
        arrow(QPointF(10, 4), QPointF(10, 16));
        arrow(QPointF(4, 10), QPointF(16, 10));
    } else if (id == "copy" || id == "copyLinear" || id == "moveLinear") {
        rect(3, 3, 9, 9);
        rect(8, 8, 9, 9);
    } else if (id == "offset") {
        line(5, 4, 5, 16);
        line(11, 4, 11, 16);
        arrow(QPointF(8.5, 10), QPointF(10.5, 10));
    } else if (id == "mirror") {
        line(10, 3, 10, 17);
        p.drawPolygon(QPolygonF({QPointF(4, 5), QPointF(8, 10), QPointF(4, 15)}));
        p.drawPolygon(QPolygonF({QPointF(16, 5), QPointF(12, 10), QPointF(16, 15)}));
    } else if (id == "delete") {
        rect(6, 6, 8, 11);
        line(4, 6, 16, 6);
        line(8, 3, 12, 3);
    } else if (id == "linearArray") {
        for (double x : {3.0, 8.0, 13.0}) rect(x, 7, 5, 7);
    } else if (id == "polarArray") {
        p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
        for (int i = 0; i < 4; ++i) {
            const double a = i * 1.5708;
            p.setBrush(color);
            p.drawEllipse(QPointF(10 + 6.5 * std::cos(a), 10 + 6.5 * std::sin(a)), 1.4, 1.4);
            p.setBrush(Qt::NoBrush);
        }
    } else if (id == "trim") {
        line(4, 4, 16, 16);
        line(16, 4, 4, 16);
        p.drawEllipse(QPointF(10, 10), 6, 6);
    } else if (id == "extend") {
        line(16, 3, 16, 17);
        arrow(QPointF(4, 10), QPointF(15, 10));
    } else if (id == "fillet") {
        p.drawPolyline(QPolygonF({QPointF(4, 16), QPointF(4, 7)}));
        p.drawArc(QRectF(4, 4, 9, 9), 90 * 16, 90 * 16);
        p.drawPolyline(QPolygonF({QPointF(13, 4), QPointF(16, 4)}));
    } else if (id == "bolt") {
        p.drawPolygon(QPolygonF({QPointF(10, 3), QPointF(15, 6), QPointF(15, 12),
                                 QPointF(10, 15), QPointF(5, 12), QPointF(5, 6)}));
        line(10, 15, 10, 18);
    } else if (id == "weld") {
        p.drawPolygon(QPolygonF({QPointF(4, 16), QPointF(16, 16), QPointF(4, 8)}));
    } else if (id == "plate") {
        rect(3, 5, 14, 10);
        p.setBrush(color);
        for (double x : {7.0, 13.0}) {
            p.drawEllipse(QPointF(x, 8), 1.1, 1.1);
            p.drawEllipse(QPointF(x, 12), 1.1, 1.1);
        }
        p.setBrush(Qt::NoBrush);
    } else if (id == "catalog") {
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) rect(4 + c * 7.0, 4 + r * 7.0, 5, 5);
    } else if (id == "bridge") {
        p.drawArc(QRectF(3, 6, 14, 12), 0, 180 * 16);
        line(3, 16, 17, 16);
        line(10, 6, 10, 3);
    } else if (id == "plane" || id == "direction") {
        p.drawPolygon(QPolygonF({QPointF(3, 12), QPointF(8, 5), QPointF(17, 5),
                                 QPointF(12, 12)}));
        line(6, 10, 12, 10);
    } else if (id == "eye") {
        p.drawArc(QRectF(3, 6, 14, 8), 0, 180 * 16);
        p.drawArc(QRectF(3, 6, 14, 8), 180 * 16, 180 * 16);
        p.setBrush(color);
        p.drawEllipse(QPointF(10, 10), 2, 2);
        p.setBrush(Qt::NoBrush);
    } else if (id == "style") {
        p.drawEllipse(QPointF(10, 10), 6, 6);
        p.setBrush(color);
        p.drawPie(QRectF(4, 4, 12, 12), 90 * 16, 180 * 16);
        p.setBrush(Qt::NoBrush);
    } else if (id == "target") {
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        line(10, 2, 10, 6);
        line(10, 14, 10, 18);
        line(2, 10, 6, 10);
        line(14, 10, 18, 10);
    } else if (id == "ghost") {
        QPen dashed = pen;
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        rect(4, 4, 12, 12);
    } else if (id == "reset") {
        p.drawArc(QRectF(4, 4, 12, 12), 40 * 16, 280 * 16);
        line(16, 8, 16, 4);
        line(16, 4, 12, 4);
    } else if (id == "extents") {
        line(3, 7, 3, 3); line(3, 3, 7, 3);
        line(13, 3, 17, 3); line(17, 3, 17, 7);
        line(3, 13, 3, 17); line(3, 17, 7, 17);
        line(13, 17, 17, 17); line(17, 13, 17, 17);
    } else if (id == "zoomWindow") {
        rect(3, 5, 11, 9);
        p.drawEllipse(QPointF(13, 12), 3.4, 3.4);
        line(15.5, 14.5, 17.5, 16.5);
    } else if (id == "surface") {
        rect(3, 9, 9, 8);
        arrow(QPointF(8, 9), QPointF(8, 3));
    } else if (id == "toggle3d") {
        rect(3, 4, 9, 9);
        p.drawPolygon(QPolygonF({QPointF(12, 8), QPointF(17, 4), QPointF(17, 12),
                                 QPointF(12, 16)}));
    } else if (id == "benchmark" || id == "gauge") {
        p.drawArc(QRectF(3, 5, 14, 14), 0, 180 * 16);
        line(10, 12, 14, 7);
    } else if (id == "gpu") {
        rect(5, 5, 10, 10);
        for (int i = 0; i < 3; ++i) {
            line(7.0 + i * 3, 2, 7.0 + i * 3, 5);
            line(7.0 + i * 3, 15, 7.0 + i * 3, 18);
        }
    } else if (id == "snap") {
        line(10, 3, 10, 17);
        line(3, 10, 17, 10);
        rect(7.5, 7.5, 5, 5);
    } else if (id == "grid") {
        for (int i = 1; i < 3; ++i) {
            line(4.0 + i * 4, 4, 4.0 + i * 4, 16);
            line(4, 4.0 + i * 4, 16, 4.0 + i * 4);
        }
        rect(4, 4, 12, 12);
    } else if (id == "ortho") {
        p.drawPolyline(QPolygonF({QPointF(4, 16), QPointF(4, 6), QPointF(15, 6)}));
        rect(4, 10, 3, 3);
    } else if (id == "polar") {
        for (int i = 0; i < 3; ++i) {
            const double a = i * 0.6;
            line(5, 15, 5 + 11 * std::cos(a), 15 - 11 * std::sin(a));
        }
    } else if (id == "dynamic") {
        p.drawPolyline(QPolygonF({QPointF(6, 4), QPointF(6, 14), QPointF(9, 11)}));
        rect(10, 8, 7, 6);
    } else if (id == "tempPoint") {
        p.setBrush(color);
        p.drawEllipse(QPointF(10, 10), 1.6, 1.6);
        p.setBrush(Qt::NoBrush);
        line(4, 6, 6, 6); line(4, 6, 4, 8);
        line(16, 14, 14, 14); line(16, 14, 16, 12);
    } else if (id == "tree") {
        rect(3, 3, 5, 4);
        rect(11, 8, 5, 4);
        rect(11, 14, 5, 4);
        line(8, 5, 10, 5); line(10, 5, 10, 10); line(10, 10, 11, 10);
        line(10, 10, 10, 16); line(10, 16, 11, 16);
    } else if (id == "part") {
        p.drawPolygon(QPolygonF({QPointF(4, 13), QPointF(10, 5), QPointF(16, 13),
                                 QPointF(10, 17)}));
    } else if (id == "assembly") {
        rect(3, 8, 6, 6);
        rect(11, 8, 6, 6);
        line(9, 11, 11, 11);
        p.drawArc(QRectF(3, 3, 14, 14), 0, 180 * 16);
    } else if (id == "bool") {
        p.drawEllipse(QPointF(8, 10), 5, 5);
        p.drawEllipse(QPointF(12, 10), 5, 5);
    } else if (id == "window") {
        rect(3, 4, 14, 12);
        line(3, 8, 17, 8);
    } else {
        // Bilinmeyen ikon: notr cerceve (elle resim yok kurali).
        rect(4, 4, 12, 12);
    }
    p.end();
    return QIcon(pix);
}

// ---------------------------------------------------------------------------
// RibbonWidget
// ---------------------------------------------------------------------------

RibbonWidget::RibbonWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbonWidget"));
    setStyleSheet(QStringLiteral(
        "QWidget#ribbonWidget { background: #2B2B30; }"
        "QTabWidget::pane { border: none; background: #2B2B30; }"
        "QTabBar::tab { background: #1E1E24; color: #B8B8C0; padding: 5px 14px;"
        "  border-top-left-radius: 3px; border-top-right-radius: 3px; }"
        "QTabBar::tab:selected { background: #2B2B30; color: #FFFFFF; }"
        "QToolButton { color: #E8E8E8; border: none; border-radius: 3px;"
        "  background: transparent; }"
        "QToolButton:hover { background: #3C3C44; }"
        "QToolButton:pressed { background: #23232B; }"
        "QToolButton::menu-indicator { image: none; }"
        "QToolButton[ribbonToggle=\"true\"]:checked { background: #3A5A8C; }"
        "QLabel#groupTitle { color: #9FA6B2; font-size: 7.5pt; }"
        "QFrame#groupSep { color: #3C3C44; }"
        "QLineEdit#quickSearch { background: #1E1E24; color: #E8E8E8;"
        "  border: 1px solid #3C3C44; border-radius: 3px; padding: 3px 6px; }"));
}

RibbonWidget::~RibbonWidget() = default;

void RibbonWidget::setCatalog(const CommandCatalog& catalog) {
    catalog_ = catalog;
    buildUi();
    restoreState();
}

void RibbonWidget::setHost(ICommandHost* host) {
    host_ = host;
    refresh();
}

void RibbonWidget::setMenu(QMenu* menu) {
    menu_ = menu;
    if (auto* btn = quickBar_ ? quickBar_->findChild<QToolButton*>(QStringLiteral("menuButton"))
                              : nullptr) {
        btn->setMenu(menu);
    }
}

void RibbonWidget::setPersistence(bool enabled, const QString& group) {
    persistence_ = enabled;
    settingsGroup_ = group;
}

void RibbonWidget::setQuickAccess(const std::vector<std::string>& ids) {
    quickAccess_ = ids;
    if (tabs_) buildUi();  // hizli erisim satiri yeniden kurulur
}

std::vector<std::string> RibbonWidget::collectIds(const std::vector<PlanItem>& items) const {
    std::vector<std::string> out;
    for (const PlanItem& item : items) {
        if (item.kind == PlanItem::Kind::Command) out.push_back(item.id);
        else if (item.kind == PlanItem::Kind::Menu)
            for (const std::string& id : collectIds(item.children)) out.push_back(id);
    }
    return out;
}

QToolButton* RibbonWidget::makeButton(const std::string& id, bool large) {
    const CommandDef* def = catalog_.registry.find(id);
    if (!def) return nullptr;

    auto* btn = new QToolButton();
    btn->setProperty("commandId", QString::fromStdString(id));
    btn->setIcon(commandIcon(def->icon));
    btn->setIconSize(QSize(large ? 24 : kIconPx, large ? 24 : kIconPx));
    btn->setText(toQ(def->title));
    btn->setToolTip(def->tooltip.empty() ? toQ(def->title) : toQ(def->tooltip));
    btn->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
    btn->setAutoRaise(true);
    if (def->kind == CommandKind::Toggle) {
        btn->setCheckable(true);
        btn->setProperty("ribbonToggle", true);
    }
    if (large) {
        const QFontMetrics fm(btn->font());
        const int width = std::max(60, fm.horizontalAdvance(btn->text()) + kButtonPadding);
        btn->setMinimumWidth(width);
        btn->setMinimumHeight(kLargeButtonHeight);
    } else {
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(fm.horizontalAdvance(btn->text()) + kIconPx + kButtonPadding);
        btn->setMinimumHeight(kSmallButtonHeight);
    }
    QObject::connect(btn, &QToolButton::clicked, this, [this, id]() { performCommand(id); });
    return btn;
}

QToolButton* RibbonWidget::makeMenuButton(const PlanItem& node, bool large) {
    auto* btn = new QToolButton();
    btn->setProperty("commandId", QString::fromStdString(node.id));
    const QString label = node.label.empty()
                              ? (catalog_.registry.find(node.id)
                                     ? toQ(catalog_.registry.find(node.id)->title)
                                     : toQ(node.id))
                              : toQ(node.label);
    std::string iconId = node.icon;
    if (iconId.empty() && !node.children.empty()) {
        if (const CommandDef* first = catalog_.registry.find(node.children.front().id))
            iconId = first->icon;
    }
    btn->setIcon(commandIcon(iconId));
    btn->setIconSize(QSize(large ? 24 : kIconPx, large ? 24 : kIconPx));
    btn->setText(label);
    btn->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setAutoRaise(true);
    if (large) {
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(std::max(60, fm.horizontalAdvance(label) + kButtonPadding));
        btn->setMinimumHeight(kLargeButtonHeight);
    } else {
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(fm.horizontalAdvance(label) + kIconPx + kButtonPadding);
        btn->setMinimumHeight(kSmallButtonHeight);
    }

    auto* menu = new QMenu(btn);
    if (node.children.empty()) {
        // Dinamik icerik (ornek: component katalogu) — host'tan alinir.
        const std::vector<DynamicMenuItem> dynamicItems =
            host_ ? host_->dynamicMenu(node.id) : std::vector<DynamicMenuItem>{};
        for (const DynamicMenuItem& child : dynamicItems) {
            QAction* act = menu->addAction(toQ(child.label));
            if (!child.icon.empty()) act->setIcon(commandIcon(child.icon));
            QObject::connect(act, &QAction::triggered, this,
                             [this, id = child.id]() { performCommand(id); });
        }
    }
    for (const PlanItem& child : node.children) {
        if (child.kind == PlanItem::Kind::Separator) {
            menu->addSeparator();
            continue;
        }
        if (child.kind == PlanItem::Kind::Menu) {
            QMenu* sub = menu->addMenu(child.label.empty() ? toQ(child.id) : toQ(child.label));
            for (const PlanItem& grand : child.children) {
                if (grand.kind == PlanItem::Kind::Separator) {
                    sub->addSeparator();
                    continue;
                }
                const CommandDef* def = catalog_.registry.find(grand.id);
                if (!def) continue;
                QString text = toQ(def->title);
                if (!def->shortcut.empty()) text += QStringLiteral("\t") + toQ(def->shortcut);
                QAction* act = sub->addAction(text);
                QObject::connect(act, &QAction::triggered, this,
                                 [this, gid = grand.id]() { performCommand(gid); });
            }
            continue;
        }
        const CommandDef* def = catalog_.registry.find(child.id);
        if (!def) continue;
        QString text = toQ(def->title);
        if (!def->shortcut.empty()) text += QStringLiteral("\t") + toQ(def->shortcut);
        QAction* act = menu->addAction(text);
        if (def->kind == CommandKind::Toggle) act->setCheckable(true);
        QObject::connect(act, &QAction::triggered, this,
                         [this, cid = child.id]() { performCommand(cid); });
    }
    btn->setMenu(menu);
    return btn;
}

QWidget* RibbonWidget::buildTabPage(const PlanTab& tab) {
    auto* page = new QWidget();
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(4);

    for (std::size_t g = 0; g < tab.groups.size(); ++g) {
        const PlanGroup& group = tab.groups[g];
        auto* groupBox = new QWidget(page);
        auto* col = new QVBoxLayout(groupBox);
        col->setContentsMargins(2, 2, 2, 0);
        col->setSpacing(1);

        auto* items = new QWidget(groupBox);
        auto* itemsRow = new QHBoxLayout(items);
        itemsRow->setContentsMargins(0, 0, 0, 0);
        itemsRow->setSpacing(2);

        // Kucuk dugmeler dikey kolonlara 3'lu yigilir (klasik serit duzeni).
        QVBoxLayout* smallColumn = nullptr;
        auto closeSmallColumn = [&]() { smallColumn = nullptr; };
        for (const PlanItem& entry : group.items) {
            if (entry.kind == PlanItem::Kind::Separator) {
                closeSmallColumn();
                auto* line = new QFrame(items);
                line->setObjectName(QStringLiteral("groupSep"));
                line->setFrameShape(QFrame::VLine);
                line->setFrameShadow(QFrame::Plain);
                itemsRow->addWidget(line);
                continue;
            }
            const bool large = entry.large;
            QToolButton* btn = entry.kind == PlanItem::Kind::Menu
                                   ? makeMenuButton(entry, large)
                                   : makeButton(entry.id, large);
            if (!btn) continue;
            if (large) {
                closeSmallColumn();
                itemsRow->addWidget(btn);
                entries_.push_back({entry.id, btn, entry.kind == PlanItem::Kind::Menu, false});
            } else {
                if (!smallColumn) {
                    auto* column = new QWidget(items);
                    smallColumn = new QVBoxLayout(column);
                    smallColumn->setContentsMargins(0, 0, 0, 0);
                    smallColumn->setSpacing(1);
                    itemsRow->addWidget(column);
                }
                smallColumn->addWidget(btn);
                if (smallColumn->count() >= 3) closeSmallColumn();
                entries_.push_back({entry.id, btn, entry.kind == PlanItem::Kind::Menu, false});
            }
        }
        if (smallColumn) smallColumn->addStretch(1);
        col->addWidget(items, 1);

        auto* title = new QLabel(toQ(group.title), groupBox);
        title->setObjectName(QStringLiteral("groupTitle"));
        title->setAlignment(Qt::AlignHCenter);
        col->addWidget(title, 0);

        row->addWidget(groupBox);
        if (g + 1 < tab.groups.size()) {
            auto* sep = new QFrame(page);
            sep->setObjectName(QStringLiteral("groupSep"));
            sep->setFrameShape(QFrame::VLine);
            sep->setFrameShadow(QFrame::Plain);
            row->addWidget(sep);
        }
    }
    row->addStretch(1);
    return page;
}

void RibbonWidget::buildUi() {
    building_ = true;
    entries_.clear();

    // Eski icerik temizlenir (yeniden kurulum: hizli erisim/katalog degisimi).
    if (auto* old = layout()) {
        QLayoutItem* item = nullptr;
        while ((item = old->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete old;
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- Ust satir: ≡ menu + hizli erisim + arama + daralt --------------
    quickBar_ = new QWidget(this);
    auto* topRow = new QHBoxLayout(quickBar_);
    topRow->setContentsMargins(6, 2, 6, 2);
    topRow->setSpacing(4);

    auto* menuBtn = new QToolButton(quickBar_);
    menuBtn->setObjectName(QStringLiteral("menuButton"));
    menuBtn->setText(QStringLiteral("≡"));
    menuBtn->setToolTip(QStringLiteral("Menü"));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setAutoRaise(true);
    menuBtn->setStyleSheet(QStringLiteral("QToolButton { font-size: 12pt; padding: 0 8px; }"));
    if (menu_) menuBtn->setMenu(menu_);
    topRow->addWidget(menuBtn);

    for (const std::string& id : quickAccess_) {
        const CommandDef* def = catalog_.registry.find(id);
        if (!def) continue;
        auto* btn = new QToolButton(quickBar_);
        btn->setProperty("commandId", QString::fromStdString(id));
        btn->setIcon(commandIcon(def->icon));
        btn->setIconSize(QSize(16, 16));
        btn->setToolTip(toQ(def->tooltip.empty() ? def->title : def->tooltip));
        btn->setAutoRaise(true);
        QObject::connect(btn, &QToolButton::clicked, this, [this, id]() { performCommand(id); });
        topRow->addWidget(btn);
        entries_.push_back({id, btn, false, true});
    }

    topRow->addStretch(1);

    search_ = new QLineEdit(quickBar_);
    search_->setObjectName(QStringLiteral("quickSearch"));
    search_->setPlaceholderText(QStringLiteral("Komut ara..."));
    search_->setFixedWidth(190);
    search_->setClearButtonEnabled(true);
    QStringList names;
    for (const CommandDef& def : catalog_.registry.commands()) names << toQ(def.title);
    completer_ = new QCompleter(names, search_);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setFilterMode(Qt::MatchContains);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    search_->setCompleter(completer_);
    QObject::connect(search_, &QLineEdit::returnPressed, this,
                     [this]() { runSearch(search_->text()); });
    topRow->addWidget(search_);

    collapseBtn_ = new QToolButton(quickBar_);
    collapseBtn_->setText(QStringLiteral("▴"));
    collapseBtn_->setToolTip(QStringLiteral("Şeridi daralt / genişlet"));
    collapseBtn_->setAutoRaise(true);
    QObject::connect(collapseBtn_, &QToolButton::clicked, this,
                     [this]() { setCollapsed(!collapsed_); });
    topRow->addWidget(collapseBtn_);

    outer->addWidget(quickBar_);

    // ---- Sekmeler ------------------------------------------------------
    content_ = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    tabs_ = new QTabWidget(content_);
    tabs_->setObjectName(QStringLiteral("ribbonTabs"));
    tabs_->setDocumentMode(true);
    for (const PlanTab& tab : catalog_.ribbon.tabs()) {
        tabs_->addTab(buildTabPage(tab), toQ(tab.title));
    }
    QObject::connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (!building_) storeState();
    });
    contentLayout->addWidget(tabs_);
    outer->addWidget(content_, 1);

    building_ = false;
    refresh();
    setCollapsed(collapsed_);
}

void RibbonWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::FontChange && tabs_ && !building_) {
        // Yazi tipi (DPI) degisti: dugme genislikleri yeni olculere gore kurulur.
        buildUi();
        restoreState();
    }
}

void RibbonWidget::performCommand(const std::string& id) {
    emit commandTriggered(QString::fromStdString(id));
    if (host_) host_->runCommand(id);
}

void RibbonWidget::activateCommand(const std::string& id) {
    for (const Entry& entry : entries_) {
        if (entry.id == id && entry.button) {
            entry.button->click();
            return;
        }
    }
    performCommand(id);
}

std::size_t RibbonWidget::buttonCount() const { return entries_.size(); }

std::size_t RibbonWidget::menuCount() const {
    std::size_t count = 0;
    for (const Entry& entry : entries_)
        if (entry.isMenu) ++count;
    return count;
}

std::vector<std::string> RibbonWidget::commandIdsInView() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const Entry& entry : entries_)
        if (!entry.quickAccess) out.push_back(entry.id);
    // Menu cocuklari da "seritte gorunen" komutlardir (plan geregi).
    for (const PlanTab& tab : catalog_.ribbon.tabs())
        for (const PlanGroup& group : tab.groups)
            for (const PlanItem& item : group.items)
                if (item.kind == PlanItem::Kind::Menu)
                    for (const std::string& id : collectIds(item.children)) out.push_back(id);
    return out;
}

QStringList RibbonWidget::searchSuggestions(const QString& text, int limit) const {
    QStringList out;
    for (const CommandDef* def : catalog_.registry.search(text.toStdString(),
                                                         static_cast<std::size_t>(limit))) {
        out << toQ(def->title);
    }
    return out;
}

std::string RibbonWidget::runSearch(const QString& text) {
    const auto hits = catalog_.registry.search(text.toStdString(), 1);
    if (hits.empty()) return {};
    performCommand(hits.front()->id);
    return hits.front()->id;
}

int RibbonWidget::clippedLabelCount() const {
    int clipped = 0;
    for (const Entry& entry : entries_) {
        if (!entry.button) continue;
        const QString text = entry.button->text();
        if (text.isEmpty()) continue;
        const QFontMetrics fm(entry.button->font());
        int available = entry.button->width();
        if (available <= 0) available = entry.button->sizeHint().width();
        const int needed = fm.horizontalAdvance(text) + kButtonPadding;
        if (available + 1 < needed) ++clipped;
    }
    return clipped;
}

void RibbonWidget::refresh() {
    if (!host_) return;
    for (const Entry& entry : entries_) {
        if (!entry.button) continue;
        const CommandDef* def = catalog_.registry.find(entry.id);
        if (!def) continue;
        const bool enabled =
            def->enabledKey.empty() ? true : host_->isEnabled(def->enabledKey);
        entry.button->setEnabled(enabled);
        if (def->kind == CommandKind::Toggle) {
            const bool on = host_->toggleState(def->toggleKey);
            if (entry.button->isChecked() != on) {
                const QSignalBlocker blocker(entry.button);
                entry.button->setChecked(on);
            }
        }
    }
}

QString RibbonWidget::selectedTab() const {
    if (!tabs_) return {};
    return tabs_->tabText(tabs_->currentIndex());
}

void RibbonWidget::setSelectedTab(const QString& title) {
    if (!tabs_) return;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->tabText(i) == title) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
}

bool RibbonWidget::isCollapsed() const { return collapsed_; }

void RibbonWidget::setCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    if (content_) content_->setVisible(!collapsed);
    if (collapseBtn_) collapseBtn_->setText(collapsed ? QStringLiteral("▾")
                                                      : QStringLiteral("▴"));
    if (!building_) storeState();
}

void RibbonWidget::restoreState() {
    if (!persistence_) return;
    QSettings settings;
    settings.beginGroup(settingsGroup_);
    const QString tab = settings.value(QStringLiteral("tab")).toString();
    const bool collapsed = settings.value(QStringLiteral("collapsed"), false).toBool();
    settings.endGroup();
    if (!tab.isEmpty()) setSelectedTab(tab);
    setCollapsed(collapsed);
}

void RibbonWidget::storeState() {
    if (!persistence_ || building_) return;
    QSettings settings;
    settings.beginGroup(settingsGroup_);
    settings.setValue(QStringLiteral("tab"), selectedTab());
    settings.setValue(QStringLiteral("collapsed"), collapsed_);
    settings.endGroup();
}

QStringList RibbonWidget::saveScreenshots(const QString& directory) {
    QStringList written;
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return written;

    if (!isVisible()) show();
    QApplication::processEvents();

    const int original = tabs_ ? tabs_->currentIndex() : 0;
    if (tabs_) {
        for (int i = 0; i < tabs_->count(); ++i) {
            tabs_->setCurrentIndex(i);
            QApplication::processEvents();
            const QString path = dir.filePath(
                QStringLiteral("ui2-ribbon-%1.png").arg(slug(tabs_->tabText(i))));
            if (grab().save(path)) written << path;
        }
        tabs_->setCurrentIndex(original);
    }

    // Daraltilmis serit
    setCollapsed(true);
    QApplication::processEvents();
    const QString collapsedPath = dir.filePath(QStringLiteral("ui2-ribbon-collapsed.png"));
    if (grab().save(collapsedPath)) written << collapsedPath;
    setCollapsed(false);
    QApplication::processEvents();

    // Hizli arama onerisi
    if (search_) {
        search_->setText(QStringLiteral("kol"));
        QApplication::processEvents();
        const QString searchPath = dir.filePath(QStringLiteral("ui2-ribbon-search.png"));
        if (grab().save(searchPath)) written << searchPath;
        search_->clear();
    }
    return written;
}

} // namespace mm
