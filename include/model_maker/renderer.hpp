#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <windows.h>
#include <optional>
#include <string>
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
    None, Move, Copy, Offset, Mirror, Delete, LinearArray, PolarArray, Trim, Extend, Fillet
};
enum class TransformPhase { Selecting, BasePoint, Destination };

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
    std::optional<double> offsetDistance;
    double filletRadius{1.0};
    std::optional<Vec3> filletFirstPick;
    std::optional<std::size_t> arrayItemCount;
    std::vector<WireframeModel> modifierBoundaries;
    bool zoomWindowActive{};
    std::optional<POINT> zoomWindowFirstCorner;
    bool interactiveNavigation{};
    bool rasterZoomPreview{};
    double rasterZoomFactor{1.0};
    Vec2 rasterZoomOffset{};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void draw(HDC target, const RECT& client, const Document& document, const Camera& camera,
              EditMode mode, const DraftView& draft) const;

private:
    HDC ensureBackBuffer(HDC target, int width, int height) const;
    bool presentRasterZoom(HDC target, int width, int height, Vec2 offset, double factor) const;
    static RECT canvasRect(const RECT& client) noexcept;
    static void drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color);

    mutable HDC backBufferDc_{};
    mutable HBITMAP backBufferBitmap_{};
    mutable HGDIOBJ backBufferDefaultBitmap_{};
    mutable int backBufferWidth_{};
    mutable int backBufferHeight_{};
};

} // namespace mm
