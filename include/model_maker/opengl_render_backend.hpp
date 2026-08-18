#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/render_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace mm {

// ── GPU-resident line batch ──────────────────────────────────────
// Represents a single persistent GPU buffer for a batch of line segments.
// Updated only when model data changes (dirty flag / version check).

struct GpuLineBatch {
    // Vertex layout: {float x, y, z; uint32_t color} — 16 bytes per vertex
    struct GpuVertex {
        float x, y, z;
        std::uint32_t color;
    };

    std::vector<GpuVertex> vertices;  // populated on upload
    std::vector<std::uint32_t> indices;  // GL_LINES index pairs
    std::uint32_t vao{};
    std::uint32_t vbo{};
    std::uint32_t ebo{};
    bool dirty{true};
    std::size_t modelCount{};
    std::size_t indexCount{};
    std::size_t versionTag{};  // used to detect model changes
};

// ── OpenGL Render Backend ───────────────────────────────────────
// Phase 1: GPU-resident line/wireframe rendering with persistent buffers.
// Non-line primitives fall through to GDI overlays.

class OpenGLRenderBackend final : public IRenderBackend {
public:
    OpenGLRenderBackend();
    ~OpenGLRenderBackend() override;

    // ── IRenderBackend overrides ─────────────────────────────────
    bool initialize(void* windowHandle, int initialWidth, int initialHeight) override;
    void shutdown() override;
    void resize(int width, int height) override;

    bool beginFrame(const FrameInfo& info) override;
    void endFrame() override;

    // GDI-style primitives (not used for line batching — kept for compatibility)
    RenderPenHandle createPen(std::uint32_t color, int widthPixels,
                              RenderPenStyle style = RenderPenStyle::Solid) override;
    RenderBrushHandle createSolidBrush(std::uint32_t color) override;
    RenderFontHandle createFont(int height, bool bold,
                                const wchar_t* faceName = L"Segoe UI") override;
    void deletePen(RenderPenHandle pen) override;
    void deleteBrush(RenderBrushHandle brush) override;
    void deleteFont(RenderFontHandle font) override;

    void drawLine(RenderPoint a, RenderPoint b, RenderPenHandle pen) override;
    void drawPolyline(const RenderPoint* points, int count, RenderPenHandle pen) override;
    void drawPolygon(const RenderPoint* points, int count,
                     RenderBrushHandle fill, RenderPenHandle border) override;
    void drawRectangle(const RenderRect& rect, RenderPenHandle pen) override;
    void drawFilledRect(const RenderRect& rect, RenderBrushHandle brush) override;
    void drawEllipse(RenderPoint center, int radiusX, int radiusY,
                     RenderPenHandle pen) override;
    void drawRoundedRect(const RenderRect& rect, int cornerRadius,
                         RenderBrushHandle fill, RenderPenHandle border) override;
    void drawAlphaPolygon(const RenderPoint* points, int count,
                          std::uint32_t color, int alpha) override;
    void drawAlphaRect(const RenderRect& rect, std::uint32_t color, int alpha) override;
    void drawText(int x, int y, const wchar_t* text, std::uint32_t color,
                  RenderFontHandle font) override;
    bool presentRasterZoom(const FrameInfo& info) override;

    bool isHardwareAccelerated() const noexcept override { return initialized_; }
    const wchar_t* backendName() const noexcept override { return L"OpenGL"; }

    // ── GPU batch rendering (Phase 1: LINE) ─────────────────────
    // Batches all wireframe models into a single (or few) draw call(s).
    // World→screen transform handled in vertex shader via uniform.
    // Buffer only re-uploaded when model data changes (dirty detection).
    void renderWireframeBatch(
        const std::vector<std::pair<std::size_t, WireframeModel>>& models,
        const Camera& camera);

    // ── FBO → GDI compositing ────────────────────────────────────
    // After endFrame(), call this to BitBlt the GL-rendered content
    // (from the FBO readback) onto the target GDI DC.
    bool blitToDC(void* target, int width, int height);

    // ── FBO composite frame (tek cagrida) ────────────────────────
    // Offscreen GL render (seffaf FBO) + GDI AlphaBlend hedef DC'ye:
    // GL pencere yuzeyine HIC cizmez — surucu-bagimsiz GDI+GL interop,
    // Qt/thread guvenligi, sunum tek GDI hattindan.
    bool renderBatchToDc(
        const std::vector<std::pair<std::size_t, WireframeModel>>& models,
        const Camera& camera, int width, int height, void* targetHdc);

    // ── Performance counters ─────────────────────────────────────
    std::size_t drawCallsPerFrame() const noexcept { return drawCalls_; }
    std::size_t bufferUploadBytes() const noexcept { return uploadBytes_; }
    std::size_t bufferRebuildCount() const noexcept { return rebuildCount_; }
    std::size_t renderedLineCount() const noexcept { return renderedLines_; }

private:
    bool compileShaders();
    void ensureBatch(const std::vector<std::pair<std::size_t, WireframeModel>>& models);
    void uploadBatch(GpuLineBatch& batch);
    void renderBatch(const GpuLineBatch& batch, const Camera& camera,
                     int width, int height);
    void ensureFbo(int width, int height);
    void ensureBlitBuffer(int width, int height);
    void cleanupGL();

    bool initialized_{false};
    int width_{};
    int height_{};

    // WGL/OpenGL context
    void* windowHandle_{};
    void* glContext_{};  // HGLRC
    void* deviceContext_{};  // HDC

    // Shader program
    std::uint32_t shaderProgram_{};
    std::int32_t uniformMvp_{};
    std::int32_t uniformScreenSize_{};

    // Line batch
    GpuLineBatch lineBatch_;

    // FBO for offscreen GL rendering → GDI compositing
    std::uint32_t fbo_{};
    std::uint32_t fboColor_{};
    std::uint32_t fboDepth_{};
    int fboWidth_{};
    int fboHeight_{};
    bool fboDirty_{true};
    std::vector<std::uint8_t> blitBuffer_;
    void* blitDib_{};

    // Performance counters
    std::size_t drawCalls_{};
    std::size_t uploadBytes_{};
    std::size_t rebuildCount_{};
    std::size_t renderedLines_{};
    bool perFrameMetricsReset_{false};
};

std::unique_ptr<IRenderBackend> createOpenGLRenderBackend();

} // namespace mm
