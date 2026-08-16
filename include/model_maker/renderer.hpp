#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/performance.hpp"
#include "model_maker/render_backend.hpp"

#include <windows.h>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace mm {

enum class VisualStyle { Wireframe, Solid, Transparent };

constexpr unsigned char visualStyleFaceAlpha(VisualStyle style) noexcept {
    switch (style) {
    case VisualStyle::Wireframe: return 0;
    case VisualStyle::Solid: return 255;
    case VisualStyle::Transparent: return 96;
    }
    return 0;
}

enum class EditMode { Draw2D, View3D };
enum class TransformCommand {
    None, Move, Copy, Offset, Mirror, Delete, LinearArray, PolarArray, Trim, Extend, Fillet, Rotate, Rotate3D
};
enum class TransformPhase { Selecting, BasePoint, RotateAxis, Destination };
enum class ModifierPreselectionAction {
    SelectEntities, BasePoint, DeleteEntities, PickTargets, PickSecondFilletEntity
};

constexpr ModifierPreselectionAction modifierPreselectionAction(
    TransformCommand command, std::size_t selectedCount) noexcept {
    if (selectedCount == 0 || command == TransformCommand::None)
        return ModifierPreselectionAction::SelectEntities;
    switch (command) {
    case TransformCommand::Delete:
        return ModifierPreselectionAction::DeleteEntities;
    case TransformCommand::Trim:
    case TransformCommand::Extend:
        return ModifierPreselectionAction::PickTargets;
    case TransformCommand::Fillet:
        return ModifierPreselectionAction::PickSecondFilletEntity;
    case TransformCommand::Rotate:
    case TransformCommand::Rotate3D:
        return ModifierPreselectionAction::BasePoint;
    case TransformCommand::Offset:
        return selectedCount == 1 ? ModifierPreselectionAction::BasePoint
                                  : ModifierPreselectionAction::SelectEntities;
    case TransformCommand::Move:
    case TransformCommand::Copy:
    case TransformCommand::Mirror:
    case TransformCommand::LinearArray:
    case TransformCommand::PolarArray:
        return ModifierPreselectionAction::BasePoint;
    case TransformCommand::None:
        return ModifierPreselectionAction::SelectEntities;
    }
    return ModifierPreselectionAction::SelectEntities;
}

constexpr bool modifierUsesPointCursor(TransformCommand command, TransformPhase phase,
                                       bool arrayItemCountReady = false,
                                       bool offsetDistanceReady = false) noexcept {
    if (command == TransformCommand::None || phase == TransformPhase::Selecting ||
        command == TransformCommand::Delete || command == TransformCommand::Trim ||
        command == TransformCommand::Extend || command == TransformCommand::Fillet)
        return false;
    if (command == TransformCommand::Offset)
        return phase == TransformPhase::Destination && offsetDistanceReady;
    if (command == TransformCommand::LinearArray || command == TransformCommand::PolarArray)
        return arrayItemCountReady;
    if (command == TransformCommand::Rotate3D)
        return phase == TransformPhase::BasePoint || phase == TransformPhase::RotateAxis ||
               phase == TransformPhase::Destination;
    return phase == TransformPhase::BasePoint || phase == TransformPhase::Destination;
}

constexpr bool modifierCompletesAfterCommit(TransformCommand command) noexcept {
    return command != TransformCommand::None && command != TransformCommand::Trim &&
           command != TransformCommand::Extend;
}

constexpr bool modifierRequires2DView(TransformCommand) noexcept {
    return false;
}

constexpr bool commandAllowsSnapping(bool drawingCommandActive, TransformCommand command,
                                     TransformPhase phase, bool arrayItemCountReady = false,
                                     bool offsetDistanceReady = false) noexcept {
    return drawingCommandActive ||
           modifierUsesPointCursor(command, phase, arrayItemCountReady, offsetDistanceReady);
}

constexpr bool commandShowsSnapFeedback(bool drawingCommandActive, bool workPlanePicking,
                                        TransformCommand command, TransformPhase phase,
                                        bool arrayItemCountReady = false,
                                        bool offsetDistanceReady = false) noexcept {
    return drawingCommandActive || workPlanePicking ||
           modifierUsesPointCursor(command, phase, arrayItemCountReady, offsetDistanceReady);
}

constexpr bool shouldRepeatLastModifierOnEnter(bool drawingCommandActive, bool inputPending,
                                                bool otherModalInteractionActive) noexcept {
    return !drawingCommandActive && !inputPending && !otherModalInteractionActive;
}

struct WorkPlaneAxisGlyph {
    Vec3 origin{};
    Vec3 x{};
    Vec3 y{};
    Vec3 z{};
};

inline WorkPlaneAxisGlyph workPlaneAxisGlyph(const WorkPlane& plane, double length) noexcept {
    return {plane.origin, plane.origin + plane.u * length,
            plane.origin + plane.v * length, plane.origin + plane.normal * length};
}

struct DraftView {
    DrawTool tool{DrawTool::Line};
    VisualStyle visualStyle{VisualStyle::Wireframe};
    std::optional<Vec3> anchor;
    std::vector<Vec3> facePoints;
    std::optional<Vec3> cursor;
    SnapType snapType{SnapType::None};
    OrthoAxis orthoAxis{OrthoAxis::None};
    bool drawingActive{true};
    bool snapEnabled{true};
    bool gridSnapEnabled{true};
    bool polarTrackingEnabled{};
    bool polarTrackingLocked{};
    bool temporaryTrackingLocked{};
    std::vector<Vec3> temporaryTrackingPoints;
    std::vector<TrackingGuide> temporaryTrackingGuides;
    std::vector<Vec3> temporaryDerivedPoints;
    bool dynamicInputEnabled{true};
    double workPlaneZ{};
    WorkPlane workPlane{};
    bool workPlanePicking{};
    std::vector<Vec3> workPlanePoints;
    std::wstring input;
    POINT cursorScreen{};
    TransformCommand transformCommand{TransformCommand::None};
    TransformPhase transformPhase{TransformPhase::Selecting};
    std::vector<std::size_t> selectedModels;
    std::optional<POINT> selectionFirstCorner;
    std::optional<Vec3> transformBase;
    std::optional<Vec3> rotateAxis;
    std::optional<double> offsetDistance;
    double filletRadius{1.0};
    std::optional<Vec3> filletFirstPick;
    std::optional<std::size_t> arrayItemCount;
    std::vector<WireframeModel> modifierBoundaries;
    bool trimExtendPreviewSuppressed{false};
    bool zoomWindowActive{};
    std::optional<POINT> zoomWindowFirstCorner;
    bool interactiveNavigation{};
    // Fare hover hareketi sırasında: temel geometri önbellekli tampondan
    // basılır (kamera sabit), yalnızca geri bildirim katmanı yeniden çizilir.
    bool motionOverlay{};
    bool rasterZoomPreview{};
    bool performanceOverlayEnabled{};
    double rasterZoomFactor{1.0};
    const std::unordered_map<std::string, NodeConstraint>* nodeConstraints{};
    const std::unordered_set<std::string>* selectedNodeKeys{};
    bool nodeConstraintsVisible{};
    std::optional<POINT> nodeSelectionFirstCorner;
    const std::unordered_map<std::size_t, BeamLoad>* beamLoads{};
    Vec2 rasterZoomOffset{};
    // Lightweight repaint: only snap markers, skip model rendering
    bool snapOnly{};
    // OpenSees result visualization
    int resultView{0}; // 0=None, 1=Deformed, 2=My, 3=Mz, 4=N, 5=Vy, 6=Vz
    double resultScale{1.0};
    const std::vector<Vec3>* nodeDisplacements{nullptr};
    const std::vector<double>* elementForces{nullptr};
    bool resultsLoaded{false};
    // Z-depth clipping relative to workplane
    bool depthClipEnabled{true};
    double depthClipZMin{-1e100};
    double depthClipZMax{1e100};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void draw(HDC target, const RECT& client, const Document& document, const Camera& camera,
              EditMode mode, const DraftView& draft, IRenderBackend* backend = nullptr) const;
    void setGuiOverlay(std::function<void()> callback) { guiOverlay_ = std::move(callback); }
    const FramePerformanceSample& performanceStats() const noexcept;

private:
    HDC ensureBackBuffer(HDC target, int width, int height) const;
    void ensureMotionBase(HDC target, int width, int height) const;
    bool presentRasterZoom(HDC target, int width, int height, Vec2 offset, double factor) const;
    static RECT canvasRect(const RECT& client) noexcept;
    static void drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color);

    mutable HDC backBufferDc_{};
    mutable HBITMAP backBufferBitmap_{};
    mutable HGDIOBJ backBufferDefaultBitmap_{};
    mutable int backBufferWidth_{};
    mutable int backBufferHeight_{};
    mutable HDC motionBaseDc_{};
    mutable HBITMAP motionBaseBitmap_{};
    mutable HGDIOBJ motionBaseDefaultBitmap_{};
    mutable int motionBaseWidth_{};
    mutable int motionBaseHeight_{};
    mutable bool motionBaseValid_{};
    mutable FramePerformanceTracker performanceTracker_{};
    mutable FrameIndexStampSet selectedIndexSet_{};
    mutable std::function<void()> guiOverlay_{};
    mutable std::chrono::steady_clock::time_point previousFrameTime_{};
    mutable bool hasPreviousFrameTime_{};
};

} // namespace mm
