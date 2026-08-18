#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mm {

// Platform-agnostic point for render backend
struct RenderPoint {
    int x{};
    int y{};
};

struct RenderRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
};

// Pen style for lines and borders
enum class RenderPenStyle : std::uint8_t { Solid, Dash, Dot };

// Primitive handle types (opaque to the renderer)
struct RenderPenHandle { std::uintptr_t id{}; };
struct RenderBrushHandle { std::uintptr_t id{}; };
struct RenderFontHandle { std::uintptr_t id{}; };
struct RenderBitmapHandle { std::uintptr_t id{}; };

// Information passed to the backend before rendering
struct FrameInfo {
    int width{};
    int height{};
    bool rasterZoomPreview{};
    double rasterZoomFactor{1.0};
    double rasterZoomOffsetX{};
    double rasterZoomOffsetY{};
};

// A single render command — batched by the renderer, executed by the backend
// For GDI, this maps to GDI calls. For DX11, this maps to draw calls.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // ── Lifecycle ──────────────────────────────────────────────
    virtual bool initialize(void* windowHandle, int initialWidth, int initialHeight) = 0;
    virtual void shutdown() = 0;
    // Tanı sayaçlarını sıfırla — yalnız OpenGL backend'de iş görür.
    virtual void resetDiagnostics() {}
    virtual void resize(int width, int height) = 0;

    // ── Frame management ───────────────────────────────────────
    // Called once per paint. Returns true if backend is ready.
    virtual bool beginFrame(const FrameInfo& info) = 0;
    virtual void endFrame() = 0;

    // ── Resource creation / destruction ────────────────────────
    virtual RenderPenHandle createPen(std::uint32_t color, int widthPixels,
                                      RenderPenStyle style = RenderPenStyle::Solid) = 0;
    virtual RenderBrushHandle createSolidBrush(std::uint32_t color) = 0;
    virtual RenderFontHandle createFont(int height, bool bold,
                                        const wchar_t* faceName = L"Segoe UI") = 0;
    virtual void deletePen(RenderPenHandle pen) = 0;
    virtual void deleteBrush(RenderBrushHandle brush) = 0;
    virtual void deleteFont(RenderFontHandle font) = 0;

    // ── Drawing primitives ─────────────────────────────────────
    virtual void drawLine(RenderPoint a, RenderPoint b, RenderPenHandle pen) = 0;
    virtual void drawPolyline(const RenderPoint* points, int count,
                              RenderPenHandle pen) = 0;
    virtual void drawPolygon(const RenderPoint* points, int count,
                             RenderBrushHandle fill, RenderPenHandle border) = 0;
    virtual void drawRectangle(const RenderRect& rect, RenderPenHandle pen) = 0;
    virtual void drawFilledRect(const RenderRect& rect, RenderBrushHandle brush) = 0;
    virtual void drawEllipse(RenderPoint center, int radiusX, int radiusY,
                             RenderPenHandle pen) = 0;
    virtual void drawRoundedRect(const RenderRect& rect, int cornerRadius,
                                 RenderBrushHandle fill, RenderPenHandle border) = 0;

    // ── Blending / alpha ───────────────────────────────────────
    // Draws a filled polygon with alpha blending over existing content
    virtual void drawAlphaPolygon(const RenderPoint* points, int count,
                                  std::uint32_t color, int alpha) = 0;
    // Fills a rectangle with alpha blending
    virtual void drawAlphaRect(const RenderRect& rect, std::uint32_t color, int alpha) = 0;

    // ── Text ───────────────────────────────────────────────────
    virtual void drawText(int x, int y, const wchar_t* text, std::uint32_t color,
                          RenderFontHandle font) = 0;

    // ── Raster operations ──────────────────────────────────────
    // Presents a raster zoom preview using the previous frame
    virtual bool presentRasterZoom(const FrameInfo& info) = 0;

    // ── Capabilities ───────────────────────────────────────────
    virtual bool isHardwareAccelerated() const noexcept = 0;
    virtual const wchar_t* backendName() const noexcept = 0;
};

// Helper to convert Vec3 projection result to backend point
inline RenderPoint toRenderPoint(const Vec3& projected) noexcept {
    return {static_cast<int>(projected.x),
            static_cast<int>(projected.y)};
}

inline RenderRect toRenderRect(int left, int top, int right, int bottom) noexcept {
    return {left, top, right, bottom};
}

// Factory functions for backend creation
std::unique_ptr<IRenderBackend> createGdiRenderBackend();
class OpenGLRenderBackend;
std::unique_ptr<IRenderBackend> createOpenGLRenderBackend();

} // namespace mm
