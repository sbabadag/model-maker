#include "model_maker/two_point_view.hpp"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QInputDialog>
#include <QMenu>
#include <QResizeEvent>
#include <QShowEvent>

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace mm {

TwoPointViewWidget::TwoPointViewWidget(Application& app, const ViewDefinition& view, QWidget* parent)
    : QWidget(parent), app_(&app) {
    // Native HWND: Win32 tuvali bunun cocugu olur (ana gorunusteki gomme ile
    // ayni yontem). WS_CLIPCHILDREN: Qt kendi zeminini tuvalin uzerine boyamasin.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 120);
    const HWND host = reinterpret_cast<HWND>(winId());
    if (host) {
        SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) | WS_CLIPCHILDREN);
        canvas_ = app_->createViewport(host, view);
    }
}

TwoPointViewWidget::~TwoPointViewWidget() {
    detach();
}

void TwoPointViewWidget::detach() noexcept {
    if (canvas_ && app_) app_->destroyViewport(canvas_);
    canvas_ = nullptr;
    app_ = nullptr;
}

void TwoPointViewWidget::layoutCanvas() {
    if (!canvas_ || !IsWindow(canvas_)) return;
    RECT rc{};
    GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
    SetWindowPos(canvas_, nullptr, 0, 0, std::max(1L, rc.right), std::max(1L, rc.bottom),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void TwoPointViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutCanvas();
}

void TwoPointViewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    layoutCanvas();
}

void TwoPointViewWidget::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    // MDI alt penceresi etkinlesince klavye tuvale gitsin (komut kisayollari).
    if (canvas_ && IsWindow(canvas_)) SetFocus(canvas_);
}

void TwoPointViewWidget::fitView() {
    if (app_ && canvas_) app_->fitViewport(canvas_);
}

void TwoPointViewWidget::editDepth() {
    if (!app_ || !canvas_) return;
    const auto def = app_->viewportDefinition(canvas_);
    if (!def) return;
    bool ok = false;
    const double front = QInputDialog::getDouble(this, QStringLiteral("Görünüş derinliği"),
        QStringLiteral("Ön derinlik (mm, izleyici tarafı):"), def->depthFront, 0.0, 1e7, 0, &ok);
    if (!ok) return;
    const double back = QInputDialog::getDouble(this, QStringLiteral("Görünüş derinliği"),
        QStringLiteral("Arka derinlik (mm):"), def->depthBack, 0.0, 1e7, 0, &ok);
    if (!ok || !app_ || !canvas_) return;
    app_->setViewportDepth(canvas_, front, back);
}

void TwoPointViewWidget::contextMenuEvent(QContextMenuEvent* event) {
    // Tuval sag tiki kendisi isler (komutta Enter / iptal — ana pencereyle
    // ayni). Bu menu yalniz tuval disi alanlarda (kenar) gorunur.
    QMenu menu(this);
    menu.addAction(QStringLiteral("Sığdır"), this, [this]() { fitView(); });
    menu.addAction(QStringLiteral("Görünüş derinliği..."), this, [this]() { editDepth(); });
    menu.exec(event->globalPos());
}

} // namespace mm
