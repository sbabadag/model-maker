#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <windows.h>
#include <optional>
#include <string>
#include <vector>

namespace mm {

enum class EditMode { Draw2D, View3D };
enum class TransformCommand { None, Move, Copy };
enum class TransformPhase { Selecting, BasePoint, Destination };

struct DraftView {
    DrawTool tool{DrawTool::Line};
    std::optional<Vec3> anchor;
    std::optional<Vec3> cursor;
    SnapType snapType{SnapType::None};
    bool drawingActive{true};
    bool snapEnabled{true};
    bool gridSnapEnabled{true};
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
