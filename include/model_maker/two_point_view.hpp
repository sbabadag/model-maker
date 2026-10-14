#pragma once

// IKI NOKTALI GORUNUS penceresi (Tekla "view by two points") — MDI alt penceresi.
// Ince bir kap: icinde Application::createViewport ile uretilen GERCEK Win32
// tuvali vardir (ana gorunusle ayni pencere sinifi + ayni olay isleyicisi).
// Boylece tum komutlar, kisayollar, snap, gumball, secim, Esc/Enter/Delete
// ana pencereyle BIREBIR ayni calisir — hicbiri burada yeniden yazilmaz.

#include "model_maker/application.hpp"
#include "model_maker/view_definition.hpp"

#include <QWidget>

#include <windows.h>

namespace mm {

class TwoPointViewWidget : public QWidget {
public:
    TwoPointViewWidget(Application& app, const ViewDefinition& view, QWidget* parent = nullptr);
    ~TwoPointViewWidget() override;

    bool isValid() const noexcept { return canvas_ != nullptr; }
    HWND canvasHandle() const noexcept { return canvas_; }
    void fitView();
    void editDepth();
    // Ana pencere kapanirken: Application yok edilmeden once tuvali birak.
    void detach() noexcept;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void layoutCanvas();

    Application* app_;
    HWND canvas_{};
};

} // namespace mm
