#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <windows.h>
#include <optional>
#include <string>

namespace mm {

enum class EditMode { Draw2D, View3D };

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
    std::wstring input;
    POINT cursorScreen{};
};

class Renderer {
public:
    void draw(HDC target, const RECT& client, const Document& document, const Camera& camera,
              EditMode mode, const DraftView& draft) const;

private:
    static POINT to2DScreen(const Vec3& point, const RECT& canvas) noexcept;
    static RECT canvasRect(const RECT& client) noexcept;
    static void drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color);
};

} // namespace mm
