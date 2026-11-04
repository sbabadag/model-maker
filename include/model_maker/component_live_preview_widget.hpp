#pragma once

// Native, parameter-driven interactive 3D preview of ComponentRegistry output.
// Qt GUI only. No OCC mutation, no Document edits, no screenshot assets.
// P1: shaded depth-sorted projected plates/bolts + param panel integration.
// P2: editable callouts anchored in model space, synchronized with the form.
// P3: interactive orbit/zoom/pan and Iso/Front/Side/Top camera presets.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_preview.hpp"
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
    // MM-5 (§8.2): onizlemenin tipini belirler. Varsayilan yerlesik
    // `tekla_haunch_40`; paketten yuklenen tipler bu setter ile baglanir.
    // Haunch'a ozgu kaplama (callout editörleri + berkitme ghost'lari) yalniz o
    // tip icin kurulur; diger tiplerde genel levha/bulon onizlemesi cizilir.
    void setTypeId(std::wstring typeId);
    const std::wstring& typeId() const noexcept { return typeId_; }
    // MM-6: VERIYLE TANIMLANAN ONIZLEME. Tanim paketin dialog.json'undan gelir
    // (ComponentDialogSchema::preview); bos ise yerlesik Haunch 40 tablosu kullanilir.
    void setPreviewDefinition(PreviewDefinition def);
    const PreviewDefinition& previewDefinition() const noexcept { return previewDef_; }
    void setParams(const std::vector<CompParam>& params);
    void setHighlightedParam(const std::wstring& name);
    void setOnEdit(std::function<void(const std::wstring&,double)> cb);
    void setView(const std::string& view);  // iso/front/side/top
    void setCompact(bool compact);  // thumbnail: same plan, no editor overlay
    bool isCompact() const { return compact_; }
    void setOnPick(std::function<void()> cb);
    // Alt+click on any stiffener (grey ghost = off, coloured = on) flips its
    // create flag. The owner mirrors it into the real form editor.
    void setOnToggle(std::function<void(const std::wstring&,bool)> cb);
    // Role keys of currently OFF stiffeners drawn as grey ghosts.
    std::vector<std::string> ghostStiffenerRoles() const;
    // Stiffener (ghost or active) under a widget position, "" if none.
    std::string stiffenerRoleAt(QPoint pos) const;
    // Flip the create flag of the stiffener under pos. False if none.
    bool toggleStiffenerAt(QPoint pos);
    // Projected centre of a grey ghost stiffener ((-1,-1) if absent).
    QPointF ghostScreenAnchor(const std::string& role) const;
    std::wstring highlightedParam() const { return highlight_; }
    // P4: hit-selected component role; empty means no overlay.
    std::string activeRole() const { return activeRole_; }
    void selectRole(const std::string& role);  // GUI + automated tests
    // MM-7 item 3: checks ihlalini isaret eden bulon grubu roleKey'i. Bos = yok.
    // Bu role sahip bulon/levha onizlemede KIRMIZI vurgulanir.
    void setViolationRole(const std::string& role);
    std::string violationRole() const { return violationRole_; }
    std::vector<std::wstring> visibleOverlayParams() const;
    // P4B: measured from actual generated bolt origins in the selected
    // end-plate local frame (edge distances are centre-to-plate-edge, mm).
    struct BoltOverlayMetrics {
        bool valid{false};
        int count{0}, totalCount{0}, rows{0}, columns{0};
        double nominalDiameter{0.0}, holeDiameter{0.0};
        double pitchX{0.0}, pitchY{0.0};
        double leftEdge{0.0}, rightEdge{0.0};
        double topEdge{0.0}, bottomEdge{0.0};
        double xmin{0.0}, xmax{0.0}, ymin{0.0}, ymax{0.0};
        double plateWidth{0.0}, plateHeight{0.0}, plateThickness{0.0};
        Vec3 plateCenter{}, plateU{}, plateV{}, plateAxis{};
    };
    BoltOverlayMetrics boltOverlayMetrics(const std::string& role) const;
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
    // MM-5: onizleme tipi (yerlesik varsayilan; paket tipleri setTypeId ile).
    std::wstring typeId_{L"tekla_haunch_40"};
    // MM-6: veriyle tanimlanan onizleme (bos = yerlesik Haunch tablosu).
    PreviewDefinition previewDef_;
    // Olculen (from,to) 3B noktalari — cizim icin recompute'ta tazelenir.
    struct PreviewDimState {
        Vec3 from{}, to{};
        double value{0.0};
        QString label;
        bool ok{false};
    };
    std::vector<PreviewDimState> previewDims_;
    GenerationPlan plan_;
    std::wstring previewWarning_; // invalid draft: show last valid geometry with a warning
    std::vector<CompParam> params_;
    std::vector<Callout> callouts_;
    std::wstring highlight_;
    std::string activeRole_;
    std::string violationRole_;  // MM-7 item 3: checks ihlalini isaret eden role
    struct HitRegion { QPolygonF polygon; std::string role; };
    std::vector<HitRegion> hits_;
    std::vector<std::pair<QPointF,std::string>> boltHits_;
    // Stiffeners switched OFF are still generated (one flag at a time) and
    // drawn grey; hits are kept in paint order so the topmost wins.
    struct Ghost { PlateSpec plate; std::string role; };
    std::vector<Ghost> ghosts_;
    std::vector<HitRegion> stiffHits_;
    std::function<void(const std::wstring&,bool)> onToggle_;
    bool altPress_{false};
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
    void drawBoltOverlay(QPainter& p, const BoltOverlayMetrics& m);
    Vec3 roleAnchor(const std::string& role) const;
    std::string hitRole(QPoint pos) const;
    void centerSelection();
};

} // namespace mm
