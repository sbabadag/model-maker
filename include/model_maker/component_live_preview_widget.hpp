#pragma once

// Native, parameter-driven interactive 3D preview of ComponentRegistry output.
// Qt GUI only. No OCC mutation, no Document edits, no screenshot assets.
// P1: shaded depth-sorted projected plates/bolts + param panel integration.
// P2: editable callouts anchored in model space, synchronized with the form.
// P3: interactive orbit/zoom/pan and Iso/Front/Side/Top camera presets.

#include "model_maker/component_engine.hpp"
#include <QWidget>
#include <QPointF>
#include <QPolygonF>
#include <functional>
#include <map>
#include <string>
#include <vector>

class QDoubleSpinBox;
class QPainter;
class QMouseEvent;
class QWheelEvent;

namespace mm {

class ComponentLivePreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ComponentLivePreviewWidget(QWidget* parent=nullptr);

    void setInput(ComponentInput input);
    void setParams(const std::vector<CompParam>& params);
    void setHighlightedParam(const std::wstring& name);
    void setOnEdit(std::function<void(const std::wstring&,double)> cb);
    void setView(const std::string& view);  // iso/front/side/top
    void setCompact(bool compact);  // thumbnail: same plan, no editor overlay
    void setOnPick(std::function<void()> cb);
    std::wstring highlightedParam() const { return highlight_; }
    // P4: hit-selected component role; empty means no overlay.
    std::string activeRole() const { return activeRole_; }
    void selectRole(const std::string& role);  // GUI + automated tests
    std::vector<std::wstring> visibleOverlayParams() const;
    // Read-only anchor for consistent viewport labels and pointer regressions.
    QPointF roleScreenAnchor(const std::string& role) const;
    // P6 diagnostics: all child geometry is recomputed from ONE generator plan.
    const GenerationPlan& previewPlan() const { return plan_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Callout {
        std::wstring name, shortLabel;
        std::string targetRole;
        QPointF shift;
        QDoubleSpinBox* spin{};
    };
    ComponentInput input_;
    GenerationPlan plan_;
    std::wstring previewWarning_; // invalid draft: show last valid geometry with a warning
    std::vector<CompParam> params_;
    std::vector<Callout> callouts_;
    std::wstring highlight_;
    std::string activeRole_;
    struct HitRegion { QPolygonF polygon; std::string role; };
    std::vector<HitRegion> hits_;
    std::vector<std::pair<QPointF,std::string>> boltHits_;
    bool dragging_{false};
    std::function<void(const std::wstring&,double)> onEdit_;
    bool haveInput_{false};
    bool compact_{false};
    std::string viewName_{"iso"};
    std::function<void()> onPick_;
    QPoint lastMouse_;
    QPoint mouseDown_;
    bool orbiting_{false}, panning_{false};
    double yaw_{0.64}, pitch_{0.40}, zoom_{1.0};
    QPointF pan_{0.0,0.0};
    Vec3 worldCenter_{};
    double modelScale_{1.0};

    void recompute();
    void syncEditors();
    void positionEditors();
    QPointF project(Vec3 p) const;
    void drawModel(QPainter& p);
    Vec3 roleAnchor(const std::string& role) const;
    std::string hitRole(QPoint pos) const;
    void centerSelection();
};

} // namespace mm
