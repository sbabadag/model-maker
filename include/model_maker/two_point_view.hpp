#pragma once

// IKI NOKTALI GORUNUS penceresi (Tekla "view by two points") — MDI alt penceresi.
// Ana belgeyi SALT OKUR; kendi Camera + Renderer'ini tasir (Renderer'in
// arka tampon/onbellek durumu pencere basinadir). GDI ile kendi native
// HWND'sine cizer (GL backend yok -> GL baglami/surucu riski yok).

#include "model_maker/application.hpp"
#include "model_maker/camera.hpp"
#include "model_maker/renderer.hpp"
#include "model_maker/view_definition.hpp"

#include <QWidget>

#include <cstdint>
#include <memory>

class QTimer;

namespace mm {

class TwoPointViewWidget : public QWidget {
public:
    TwoPointViewWidget(Application& app, ViewDefinition view, QWidget* parent = nullptr);
    ~TwoPointViewWidget() override;

    const ViewDefinition& definition() const noexcept { return view_; }
    void fitView();
    // Ana pencere kapanirken: belgeye bir daha dokunma.
    void detach() noexcept { app_ = nullptr; }

    QPaintEngine* paintEngine() const override { return nullptr; } // GDI kendi cizer

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void pollDocument();
    bool clientSize(int& width, int& height) const;

    Application* app_;
    ViewDefinition view_;
    Camera camera_;
    std::unique_ptr<Renderer> renderer_;
    QTimer* pollTimer_{};
    bool needsFit_{true};
    bool panning_{false};
    QPointF lastPan_{};
    std::uint64_t seenRevision_{~0ull};
    std::size_t seenModelCount_{~std::size_t{0}};
    std::size_t seenSelection_{~std::size_t{0}};
};

} // namespace mm
