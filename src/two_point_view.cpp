#include "model_maker/two_point_view.hpp"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

#include "model_maker/drafting.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace mm {

TwoPointViewWidget::TwoPointViewWidget(Application& app, ViewDefinition view, QWidget* parent)
    : QWidget(parent), app_(&app), view_(std::move(view)), renderer_(std::make_unique<Renderer>()) {
    // Kendi native HWND'si + Qt boyamasi kapali: Renderer dogrudan GDI ile cizer.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setMinimumSize(160, 120);
    camera_.setViewBasis(view_.right, view_.up, view_.origin);

    // Belge degisimini (cizim/silme/secim) yoklama: Application bildirim
    // yayinlamadigi icin revizyon + model sayisi + secim imzasi izlenir.
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(150);
    QObject::connect(pollTimer_, &QTimer::timeout, this, [this]() { pollDocument(); });
    pollTimer_->start();
}

TwoPointViewWidget::~TwoPointViewWidget() {
    if (pollTimer_) pollTimer_->stop();
}

bool TwoPointViewWidget::clientSize(int& width, int& height) const {
    RECT rc{};
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd || !GetClientRect(hwnd, &rc)) return false;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    return width > 0 && height > 0;
}

void TwoPointViewWidget::fitView() {
    if (!app_) return;
    int width = 0, height = 0;
    if (!clientSize(width, height)) { needsFit_ = true; return; } // 0x0: ilk gecerli boyutta
    const Bounds3 box = viewFitBounds(app_->document().modelBounds(), view_);
    camera_.setViewBasis(view_.right, view_.up, view_.origin);
    camera_.fit3D(box.minimum, box.maximum, width, height, 40.0);
    needsFit_ = false;
    update();
}

void TwoPointViewWidget::pollDocument() {
    if (!app_ || !isVisible()) return;
    const Document& document = app_->document();
    std::size_t selection = app_->selectedModelIndices().size();
    for (const auto index : app_->selectedModelIndices()) selection = selection * 31u + index + 1u;
    if (document.revision() == seenRevision_ && document.models().size() == seenModelCount_ &&
        selection == seenSelection_)
        return;
    seenRevision_ = document.revision();
    seenModelCount_ = document.models().size();
    seenSelection_ = selection;
    update();
}

void TwoPointViewWidget::paintEvent(QPaintEvent*) {
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;
    int width = 0, height = 0;
    if (!clientSize(width, height)) return; // simge durumu / 0x0
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    if (!app_) { // ana pencere kapaniyor: bos zemin
        RECT rc{0, 0, width, height};
        HBRUSH bg = CreateSolidBrush(canvasBackgroundColor());
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        ReleaseDC(hwnd, dc);
        return;
    }
    if (needsFit_) {
        const Bounds3 box = viewFitBounds(app_->document().modelBounds(), view_);
        camera_.fit3D(box.minimum, box.maximum, width, height, 40.0);
        needsFit_ = false;
    }
    DraftView draft;
    draft.visualStyle = app_->visualStyle();
    draft.drawingActive = false;
    draft.snapEnabled = false;
    draft.gridSnapEnabled = false;
    draft.dynamicInputEnabled = false;
    draft.depthClipEnabled = false;
    // Secim vurgusu yalniz dilimdeki secili nesneler icin (renderer secim
    // pass'i dilim filtresinden gecmez).
    {
        const auto& bounds = app_->document().modelBounds();
        for (const auto index : app_->selectedModelIndices())
            if (index < bounds.size() && boundsInViewSlab(bounds[index], view_))
                draft.selectedModels.push_back(index);
    }
    draft.viewSlab = &view_;
    const RECT client{0, 0, width, height};
    try {
        // backend=nullptr -> saf GDI yolu (GL baglami yok).
        renderer_->draw(dc, client, app_->document(), camera_, EditMode::View3D, draft, nullptr);
        if (selecting_ && selectDragged_) drawSelectionRect(dc, selectStart_, selectCurrent_);
    } catch (...) {
        // Cizim hatasi ikincil pencereyi/uygulamayi dusurmesin.
    }
    ReleaseDC(hwnd, dc);
}

void TwoPointViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void TwoPointViewWidget::wheelEvent(QWheelEvent* event) {
    int width = 0, height = 0;
    if (!clientSize(width, height)) return;
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    const double dpr = devicePixelRatioF();
    const QPointF p = event->position() * dpr;
    camera_.zoom3DAt({p.x(), p.y()}, std::pow(1.15, std::clamp(steps, -5.0, 5.0)), width, height);
    update();
    event->accept();
}

POINT TwoPointViewWidget::devicePoint(const QPointF& logical) const {
    const QPointF p = logical * devicePixelRatioF();
    return POINT{static_cast<LONG>(std::lround(p.x())), static_cast<LONG>(std::lround(p.y()))};
}

void TwoPointViewWidget::selectAt(POINT p) {
    if (!app_) return;
    int width = 0, height = 0;
    if (!clientSize(width, height)) return;
    const Document& document = app_->document();
    const auto candidates = viewSlabCandidates(document, view_);
    const auto hit = hitTestModelCandidates3D({static_cast<double>(p.x), static_cast<double>(p.y)},
                                              document, camera_, width, height, 10.0, candidates);
    // Bos alana tik: Tekla gibi secimi temizle; nesneye tik: ekle/cikar.
    if (hit) app_->applyViewSelection(Application::ViewSelectOp::Toggle, {*hit});
    else app_->applyViewSelection(Application::ViewSelectOp::Clear, {});
}

void TwoPointViewWidget::finishWindowSelection(POINT second) {
    if (!app_) return;
    int width = 0, height = 0;
    if (!clientSize(width, height)) return;
    if (selectStart_.x == second.x || selectStart_.y == second.y) return; // cizgi kalinliginda kutu
    const bool crossing = second.x < selectStart_.x; // sagdan sola = crossing (AutoCAD/Tekla)
    const Document& document = app_->document();
    const auto hits = selectModelsInRect3D(
        {static_cast<double>(selectStart_.x), static_cast<double>(selectStart_.y)},
        {static_cast<double>(second.x), static_cast<double>(second.y)},
        document, camera_, width, height, crossing);
    app_->applyViewSelection(Application::ViewSelectOp::Add, filterToViewSlab(hits, document, view_));
}

void TwoPointViewWidget::drawSelectionRect(HDC dc, POINT first, POINT second) const {
    // Ana penceredeki secim kutusuyla ayni gorunum: mavi dolu cizgi = window,
    // yesil kesikli = crossing.
    const bool crossing = second.x < first.x;
    const RECT r{std::min(first.x, second.x), std::min(first.y, second.y),
                 std::max(first.x, second.x), std::max(first.y, second.y)};
    HPEN pen = CreatePen(crossing ? PS_DASH : PS_SOLID, 1,
                         crossing ? RGB(34, 139, 74) : RGB(36, 104, 181));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right + 1, r.bottom + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void TwoPointViewWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    // Orta tus (veya Alt + sol) = kaydir. Gorunus SABIT yonludur (Tekla
    // gorunusu gibi) -> dondurme yok.
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        panning_ = true;
        lastPan_ = event->position() * devicePixelRatioF();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && app_) {
        selecting_ = true;
        selectDragged_ = false;
        selectStart_ = selectCurrent_ = devicePoint(event->position());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TwoPointViewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (selecting_) {
        selectCurrent_ = devicePoint(event->position());
        // 4 px esik: titreyen tik pencere secimine donmesin.
        if (std::abs(selectCurrent_.x - selectStart_.x) > 4 ||
            std::abs(selectCurrent_.y - selectStart_.y) > 4)
            selectDragged_ = true;
        if (selectDragged_) update();
        return;
    }
    if (!panning_) { QWidget::mouseMoveEvent(event); return; }
    const QPointF p = event->position() * devicePixelRatioF();
    camera_.pan3DByPixels(p.x() - lastPan_.x(), p.y() - lastPan_.y());
    lastPan_ = p;
    update();
}

void TwoPointViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        return;
    }
    if (selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        const POINT end = devicePoint(event->position());
        if (selectDragged_) finishWindowSelection(end);
        else selectAt(selectStart_);
        selectDragged_ = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TwoPointViewWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) { fitView(); return; } // orta cift tik = sigdir
    QWidget::mouseDoubleClickEvent(event);
}

void TwoPointViewWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F || event->key() == Qt::Key_Home) { fitView(); return; }
    if (event->key() == Qt::Key_Delete && app_) {
        // Ana pencereyle ayni: secim varsa hemen siler (tek undo adimi).
        // Secim yoksa ana pencerede bekleyen bir Delete komutu ACMA.
        if (app_->hasSelection()) app_->startTransformCommand(TransformCommand::Delete);
        update();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && app_) {
        app_->confirmFromView();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (selecting_) { selecting_ = false; selectDragged_ = false; update(); return; }
        if (app_) app_->applyViewSelection(Application::ViewSelectOp::Clear, {});
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TwoPointViewWidget::focusOutEvent(QFocusEvent* event) {
    // Pencere disina tiklanip odak kayarsa yarim kalan secim kutusu kalmasin.
    if (selecting_) { selecting_ = false; selectDragged_ = false; update(); }
    QWidget::focusOutEvent(event);
}

void TwoPointViewWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addAction(QStringLiteral("Sığdır (F)"), this, [this]() { fitView(); });
    menu.addAction(QStringLiteral("Görünüş derinliği..."), this, [this]() {
        bool ok = false;
        const double front = QInputDialog::getDouble(this, QStringLiteral("Görünüş derinliği"),
            QStringLiteral("Ön derinlik (mm, izleyici tarafı):"), view_.depthFront, 0.0, 1e7, 0, &ok);
        if (!ok) return;
        const double back = QInputDialog::getDouble(this, QStringLiteral("Görünüş derinliği"),
            QStringLiteral("Arka derinlik (mm):"), view_.depthBack, 0.0, 1e7, 0, &ok);
        if (!ok) return;
        if (std::isfinite(front) && front >= 0.0) view_.depthFront = front;
        if (std::isfinite(back) && back >= 0.0) view_.depthBack = back;
        update();
    });
    menu.exec(event->globalPos());
}

} // namespace mm
