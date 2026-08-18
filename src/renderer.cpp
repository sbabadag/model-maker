#include "model_maker/renderer.hpp"
#include "model_maker/opengl_render_backend.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <map>
#include <string>
#include <tuple>

namespace mm {
namespace {
void line(HDC dc, int x1, int y1, int x2, int y2) {
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

COLORREF nativeColor(std::uint32_t rgb) noexcept {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

COLORREF shadedFaceColor(std::uint32_t rgb) noexcept {
    constexpr double factor = 0.42;
    return RGB(static_cast<int>(((rgb >> 16) & 0xFF) * factor),
               static_cast<int>(((rgb >> 8) & 0xFF) * factor),
               static_cast<int>((rgb & 0xFF) * factor));
}

int lineWeightPixels(int hundredthsMillimeter) noexcept {
    if (hundredthsMillimeter <= 0) return 0;
    if (hundredthsMillimeter <= 25) return 2;
    if (hundredthsMillimeter <= 50) return 3;
    if (hundredthsMillimeter <= 100) return 5;
    if (hundredthsMillimeter <= 200) return 7;
    return 9;
}

HPEN createEntityPen(const EntityProperties& properties) {
    const COLORREF color = nativeColor(properties.effectiveColor);
    const int width = lineWeightPixels(properties.effectiveLineWeight);
    std::string type = properties.effectiveLineType;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (type == "CONTINUOUS" || type == "BYLAYER" || type == "BYBLOCK")
        return CreatePen(PS_SOLID, width, color);
    const double scale = std::clamp(properties.lineTypeScale, 0.1, 100.0);
    std::vector<DWORD> pattern;
    const auto scaled = [&](double value) {
        return static_cast<DWORD>(std::max(1.0, std::round(value * scale)));
    };
    if (type.find("CENTER") != std::string::npos) pattern = {scaled(12), scaled(4), scaled(2), scaled(4)};
    else if (type.find("DOT") != std::string::npos) pattern = {scaled(1), scaled(3)};
    else pattern = {scaled(8), scaled(4)};
    LOGBRUSH brush{BS_SOLID, color, 0};
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT | PS_JOIN_ROUND,
                            width, &brush, static_cast<DWORD>(pattern.size()), pattern.data());
    return pen ? pen : CreatePen(PS_SOLID, width, color);
}
}

RECT Renderer::canvasRect(const RECT& client) noexcept {
    return client;
}

Renderer::~Renderer() {
    if (backBufferDc_) {
        if (backBufferBitmap_) {
            SelectObject(backBufferDc_, backBufferDefaultBitmap_);
            DeleteObject(backBufferBitmap_);
        }
        DeleteDC(backBufferDc_);
    }
    if (motionBaseDc_) {
        if (motionBaseBitmap_) {
            SelectObject(motionBaseDc_, motionBaseDefaultBitmap_);
            DeleteObject(motionBaseBitmap_);
        }
        DeleteDC(motionBaseDc_);
    }
}

void Renderer::ensureMotionBase(HDC target, int width, int height) const {
    if (motionBaseDc_ && motionBaseWidth_ == width && motionBaseHeight_ == height) return;
    if (motionBaseDc_) {
        if (motionBaseBitmap_) {
            SelectObject(motionBaseDc_, motionBaseDefaultBitmap_);
            DeleteObject(motionBaseBitmap_);
        }
        DeleteDC(motionBaseDc_);
    }
    motionBaseDc_ = CreateCompatibleDC(target);
    motionBaseBitmap_ = CreateCompatibleBitmap(target, width, height);
    motionBaseDefaultBitmap_ = SelectObject(motionBaseDc_, motionBaseBitmap_);
    motionBaseWidth_ = width;
    motionBaseHeight_ = height;
    motionBaseValid_ = false;
}

HDC Renderer::ensureBackBuffer(HDC target, int width, int height) const {
    if (!backBufferDc_) {
        backBufferDc_ = CreateCompatibleDC(target);
        if (!backBufferDc_) return nullptr;
        backBufferDefaultBitmap_ = GetCurrentObject(backBufferDc_, OBJ_BITMAP);
    }
    if (backBufferBitmap_ && width == backBufferWidth_ && height == backBufferHeight_)
        return backBufferDc_;

    HBITMAP replacement = CreateCompatibleBitmap(target, width, height);
    if (!replacement) return nullptr;
    SelectObject(backBufferDc_, replacement);
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    backBufferBitmap_ = replacement;
    backBufferWidth_ = width;
    backBufferHeight_ = height;
    return backBufferDc_;
}

bool Renderer::presentRasterZoom(HDC target, int width, int height, Vec2 offset, double factor) const {
    if (!backBufferBitmap_ || backBufferWidth_ != width || backBufferHeight_ != height || factor <= 0.0)
        return false;
    const int destinationX = static_cast<int>(std::lround(offset.x));
    const int destinationY = static_cast<int>(std::lround(offset.y));
    const int destinationWidth = std::max(1, static_cast<int>(std::lround(width * factor)));
    const int destinationHeight = std::max(1, static_cast<int>(std::lround(height * factor)));
    HBRUSH background = CreateSolidBrush(RGB(15, 18, 26));
    if (destinationX > 0) {
        RECT border{0, 0, std::min(width, destinationX), height}; FillRect(target, &border, background);
    }
    if (destinationY > 0) {
        RECT border{0, 0, width, std::min(height, destinationY)}; FillRect(target, &border, background);
    }
    if (destinationX + destinationWidth < width) {
        RECT border{std::max(0, destinationX + destinationWidth), 0, width, height}; FillRect(target, &border, background);
    }
    if (destinationY + destinationHeight < height) {
        RECT border{0, std::max(0, destinationY + destinationHeight), width, height}; FillRect(target, &border, background);
    }
    DeleteObject(background);
    SetStretchBltMode(target, COLORONCOLOR);
    StretchBlt(target, destinationX, destinationY, destinationWidth, destinationHeight,
               backBufferDc_, 0, 0, width, height, SRCCOPY);
    return true;
}

void Renderer::drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, text, static_cast<int>(wcslen(text)));
}

void Renderer::draw(HDC target, const RECT& client, const Document& document, const Camera& camera,
                    EditMode mode, const DraftView& draft, IRenderBackend* backend) const {
    // Phase 1 GPU-retained rendering: detect OpenGL backend
    const bool useGpuLines = backend && backend->isHardwareAccelerated();
    auto* glBackend = useGpuLines ? static_cast<OpenGLRenderBackend*>(backend) : nullptr;
    const auto frameStart = std::chrono::steady_clock::now();
    const double frameIntervalSeconds = hasPreviousFrameTime_
        ? std::chrono::duration<double>(frameStart - previousFrameTime_).count() : 0.0;
    previousFrameTime_ = frameStart;
    hasPreviousFrameTime_ = true;
    FramePerformanceSample performance;
    performance.totalEntities = document.models().size();
    const auto finishPerformanceSample = [&](bool rasterPreview) {
        performance.rasterPreview = rasterPreview;
        performance.cpuFrameMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frameStart).count();
        performanceTracker_.record(performance, frameIntervalSeconds);
    };
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    if (draft.rasterZoomPreview &&
        presentRasterZoom(target, width, height, draft.rasterZoomOffset, draft.rasterZoomFactor)) {
        performance.drawCalls = 1;
        finishPerformanceSample(true);
        return;
    }
    HDC dc = draft.snapOnly ? target : ensureBackBuffer(target, width, height);
    if (!dc) return;

    if (!draft.snapOnly) {
    HBRUSH background = CreateSolidBrush(RGB(15, 18, 26));
    FillRect(dc, &client, background);
    DeleteObject(background);
    }
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    const RECT canvas = canvasRect(client);
    const auto projectPoint = [&](const Vec3& point) {
        const auto projected = mode == EditMode::Draw2D
            ? camera.project2D(point, canvas.right - canvas.left, canvas.bottom - canvas.top)
            : camera.project(point, canvas.right - canvas.left, canvas.bottom - canvas.top);
        return POINT{static_cast<LONG>(projected.x + canvas.left),
                     static_cast<LONG>(projected.y + canvas.top)};
    };
    HGDIOBJ stockPen = GetCurrentObject(dc, OBJ_PEN);
    {
        HPEN gridPen = CreatePen(PS_SOLID, 1,
                                 mode == EditMode::View3D ? RGB(66, 68, 72) : RGB(34, 40, 53));
        SelectObject(dc, gridPen);
        if (mode == EditMode::Draw2D) {
            const POINT origin = projectPoint({0.0, 0.0, 0.0});
            const POINT unit = projectPoint({1.0, 0.0, 0.0});
            const int gridSpacing = std::max(1, static_cast<int>(std::abs(unit.x - origin.x)));
            for (int x = origin.x % gridSpacing; x < canvas.right; x += gridSpacing)
                line(dc, x, canvas.top, x, canvas.bottom);
            for (int y = origin.y % gridSpacing; y < canvas.bottom; y += gridSpacing)
                line(dc, canvas.left, y, canvas.right, y);
        } else {
            constexpr int gridExtent = 10;
            for (int coordinate = -gridExtent; coordinate <= gridExtent; ++coordinate) {
                const POINT verticalA = projectPoint(draft.workPlane.fromPlane({static_cast<double>(coordinate), -gridExtent}));
                const POINT verticalB = projectPoint(draft.workPlane.fromPlane({static_cast<double>(coordinate), gridExtent}));
                const POINT horizontalA = projectPoint(draft.workPlane.fromPlane({-gridExtent, static_cast<double>(coordinate)}));
                const POINT horizontalB = projectPoint(draft.workPlane.fromPlane({gridExtent, static_cast<double>(coordinate)}));
                line(dc, verticalA.x, verticalA.y, verticalB.x, verticalB.y);
                line(dc, horizontalA.x, horizontalA.y, horizontalB.x, horizontalB.y);
            }
        }
        SelectObject(dc, stockPen);
        DeleteObject(gridPen);

        const auto axisGlyph = workPlaneAxisGlyph(draft.workPlane, 2.0);
        const POINT axisOrigin = projectPoint(axisGlyph.origin);
        const auto drawAxisArrow = [&](const Vec3& worldEnd, COLORREF color, const wchar_t* label) {
            const POINT end = projectPoint(worldEnd);
            const double dx = static_cast<double>(end.x - axisOrigin.x);
            const double dy = static_cast<double>(end.y - axisOrigin.y);
            const double length = std::hypot(dx, dy);
            HPEN pen = CreatePen(PS_SOLID, 2, color);
            SelectObject(dc, pen);
            if (length >= 7.0) {
                line(dc, axisOrigin.x, axisOrigin.y, end.x, end.y);
                const double ux = dx / length;
                const double uy = dy / length;
                const POINT headA{static_cast<LONG>(std::lround(end.x - ux * 8.0 - uy * 4.0)),
                                  static_cast<LONG>(std::lround(end.y - uy * 8.0 + ux * 4.0))};
                const POINT headB{static_cast<LONG>(std::lround(end.x - ux * 8.0 + uy * 4.0)),
                                  static_cast<LONG>(std::lround(end.y - uy * 8.0 - ux * 4.0))};
                line(dc, end.x, end.y, headA.x, headA.y);
                line(dc, end.x, end.y, headB.x, headB.y);
                drawText(dc, end.x + static_cast<int>(std::lround(ux * 5.0)) - 4,
                         end.y + static_cast<int>(std::lround(uy * 5.0)) - 8, label, color);
            } else {
                HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Ellipse(dc, axisOrigin.x - 5, axisOrigin.y - 5,
                        axisOrigin.x + 6, axisOrigin.y + 6);
                SelectObject(dc, oldBrush);
                SetPixel(dc, axisOrigin.x, axisOrigin.y, color);
                drawText(dc, axisOrigin.x + 7, axisOrigin.y - 10, label, color);
            }
            SelectObject(dc, stockPen);
            DeleteObject(pen);
        };
        drawAxisArrow(axisGlyph.x, RGB(235, 82, 96), L"X");
        drawAxisArrow(axisGlyph.y, RGB(72, 211, 121), L"Y");
        drawAxisArrow(axisGlyph.z, RGB(78, 148, 255), L"Z");

        HPEN basePointPen = CreatePen(PS_SOLID, 1, RGB(190, 194, 202));
        SelectObject(dc, basePointPen);
        HGDIOBJ oldBaseBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, axisOrigin.x - 3, axisOrigin.y - 3, axisOrigin.x + 4, axisOrigin.y + 4);
        SelectObject(dc, oldBaseBrush);
        SelectObject(dc, stockPen);
        DeleteObject(basePointPen);
    } // grid + eksenler: interaktif karelerde de çizilir (flicker önleme)

    std::vector<POINT> projectedChain;
    std::size_t interactiveModelStride = 1;
    const auto drawModel = [&](const WireframeModel& model, const Vec3& offset = Vec3{}) {
        const auto& vertices = model.vertices();
        const auto& edges = model.edges();
        const std::size_t stride = draft.interactiveNavigation
            ? std::max<std::size_t>(1, (vertices.size() + 120'000 - 1) / 120'000) : 1;
        bool chain = vertices.size() >= 2 &&
                     (edges.size() == vertices.size() - 1 || edges.size() == vertices.size());
        if (chain) {
            for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
                if (edges[i] != Edge{i, i + 1}) { chain = false; break; }
            }
            if (chain && edges.size() == vertices.size() &&
                edges.back() != Edge{vertices.size() - 1, 0}) chain = false;
        }
        if (chain) {
            const auto previousCapacity = projectedChain.capacity();
            projectedChain.clear();
            projectedChain.reserve(vertices.size() / stride + 2);
            if (projectedChain.capacity() > previousCapacity) ++performance.frameBufferGrowths;
            for (std::size_t index = 0; index < vertices.size(); index += stride)
                projectedChain.push_back(projectPoint(vertices[index] + offset));
            if ((vertices.size() - 1) % stride != 0)
                projectedChain.push_back(projectPoint(vertices.back() + offset));
            if (edges.size() == vertices.size()) projectedChain.push_back(projectedChain.front());
            constexpr std::size_t chunkSize = 8192;
            for (std::size_t begin = 0; begin + 1 < projectedChain.size(); begin += chunkSize - 1) {
                const std::size_t count = std::min(chunkSize, projectedChain.size() - begin);
                Polyline(dc, projectedChain.data() + begin, static_cast<int>(count));
                ++performance.drawCalls;
                performance.projectedVertices += count;
                if (begin + count == projectedChain.size()) break;
            }
            return;
        }
        for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); edgeIndex += stride) {
            const auto& edge = edges[edgeIndex];
            const POINT from = projectPoint(vertices[edge.from] + offset);
            const POINT to = projectPoint(vertices[edge.to] + offset);
            line(dc, from.x, from.y, to.x, to.y);
            ++performance.drawCalls;
            performance.projectedVertices += 2;
        }
    };
    if (selectedIndexSet_.assign(document.models().size(), draft.selectedModels))
        ++performance.frameBufferGrowths;
    const auto isSelected = [&](std::size_t index) {
        return selectedIndexSet_.contains(index);
    };

    // Hareket kareleri geri bildirim katmanı: seçim vurgusu, transform
    // izleyicisi/hayaletleri, trim/extend önizlemesi, çizim lastik bandı,
    // snap işareti ve dinamik giriş. Temel geometri motion tabanından gelir.
    const auto drawMotionFeedback = [&]() {
        const bool motionDrafting = commandShowsSnapFeedback(
            draft.drawingActive, draft.workPlanePicking, draft.transformCommand,
            draft.transformPhase, draft.arrayItemCount.has_value(),
            draft.offsetDistance.has_value());
        const bool feedbackActive = draft.transformCommand != TransformCommand::None ||
                                    !draft.selectedModels.empty() ||
                                    (draft.drawingActive && draft.anchor && draft.cursor);
        if (feedbackActive) {
            if (!draft.selectedModels.empty()) {
                HPEN selectedPen = CreatePen(PS_SOLID, 3, RGB(90, 255, 145));
                SelectObject(dc, selectedPen);
                for (const auto index : draft.selectedModels) {
                    if (index < document.models().size() && document.modelIsEditable(index))
                        drawModel(document.models()[index]);
                }
                SelectObject(dc, stockPen);
                DeleteObject(selectedPen);
            }
            if (draft.transformCommand != TransformCommand::None &&
                draft.transformPhase == TransformPhase::Destination &&
                draft.transformBase && draft.cursor) {
                const POINT basePoint = projectPoint(*draft.transformBase);
                const POINT destinationPoint = projectPoint(*draft.cursor);
                const COLORREF trackerColor = [&]() -> COLORREF {
                    switch (draft.orthoAxis) {
                    case OrthoAxis::X: return RGB(235, 82, 96);
                    case OrthoAxis::Y: return RGB(72, 211, 121);
                    case OrthoAxis::Z: return RGB(78, 148, 255);
                    default: return RGB(255, 206, 84);
                    }
                }();
                HPEN trackerPen = CreatePen(PS_DOT, 1, trackerColor);
                SelectObject(dc, trackerPen);
                line(dc, basePoint.x, basePoint.y, destinationPoint.x, destinationPoint.y);
                SelectObject(dc, stockPen);
                DeleteObject(trackerPen);

                HPEN transformPreview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
                SelectObject(dc, transformPreview);
                for (const auto index : draft.selectedModels) {
                    if (index >= document.models().size()) continue;
                    if (draft.transformCommand == TransformCommand::Mirror) {
                        const auto preview = mode == EditMode::View3D
                            ? mirrorModelOnPlane(document.models()[index], *draft.transformBase,
                                                 *draft.cursor, draft.workPlane)
                            : mirrorModel2D(document.models()[index], *draft.transformBase, *draft.cursor);
                        if (preview) drawModel(*preview);
                    } else if (draft.transformCommand == TransformCommand::LinearArray &&
                               draft.arrayItemCount) {
                        for (const auto& preview : linearArray2D(document.models()[index],
                                                                 *draft.arrayItemCount,
                                                                 *draft.cursor - *draft.transformBase))
                            drawModel(preview);
                    } else {
                        drawModel(document.models()[index], *draft.cursor - *draft.transformBase);
                    }
                }
                SelectObject(dc, stockPen);
                DeleteObject(transformPreview);
            }
            if (draft.transformCommand == TransformCommand::Offset &&
                draft.transformPhase == TransformPhase::Destination && draft.offsetDistance &&
                draft.cursor && draft.selectedModels.size() == 1 &&
                draft.selectedModels.front() < document.models().size()) {
                const auto preview = mode == EditMode::View3D
                    ? offsetModelOnPlane(document.models()[draft.selectedModels.front()],
                                         *draft.offsetDistance, *draft.cursor, draft.workPlane)
                    : offsetModel2D(document.models()[draft.selectedModels.front()],
                                    *draft.offsetDistance, *draft.cursor);
                if (preview) {
                    HPEN previewPen = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
                    SelectObject(dc, previewPen);
                    drawModel(*preview);
                    SelectObject(dc, stockPen);
                    DeleteObject(previewPen);
                }
            }
            if (draft.workPlanePicking) {
                HPEN pickPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
                SelectObject(dc, pickPen);
                for (std::size_t index = 1; index < draft.workPlanePoints.size(); ++index) {
                    const POINT a = projectPoint(draft.workPlanePoints[index - 1]);
                    const POINT b = projectPoint(draft.workPlanePoints[index]);
                    line(dc, a.x, a.y, b.x, b.y);
                }
                if (!draft.workPlanePoints.empty() && draft.cursor) {
                    const POINT a = projectPoint(draft.workPlanePoints.back());
                    const POINT b = projectPoint(*draft.cursor);
                    line(dc, a.x, a.y, b.x, b.y);
                }
                SelectObject(dc, stockPen);
                DeleteObject(pickPen);
            }
            if ((draft.transformCommand == TransformCommand::Trim ||
                 draft.transformCommand == TransformCommand::Extend) &&
                draft.transformPhase == TransformPhase::Destination &&
                !draft.modifierBoundaries.empty() && !draft.trimExtendPreviewSuppressed) {
                Vec3 pick{};
                std::optional<std::size_t> target;
                if (mode == EditMode::Draw2D) {
                    pick = camera.unproject2D({static_cast<double>(draft.cursorScreen.x),
                                               static_cast<double>(draft.cursorScreen.y)},
                                              width, height);
                    target = hitTestModel2D(pick, document, 10.0 / (60.0 * camera.zoom()));
                } else {
                    WorkPlane targetPlane = draft.workPlane;
                    if (!draft.modifierBoundaries.front().vertices().empty())
                        targetPlane.origin = draft.modifierBoundaries.front().vertices().front();
                    if (const auto projectedPick = camera.unprojectToPlane(
                            {static_cast<double>(draft.cursorScreen.x),
                             static_cast<double>(draft.cursorScreen.y)}, width, height, targetPlane))
                        pick = *projectedPick;
                    target = hitTestModel3D({static_cast<double>(draft.cursorScreen.x),
                                             static_cast<double>(draft.cursorScreen.y)},
                                            document, camera, width, height, 10.0);
                }
                if (target && *target < document.models().size()) {
                    HPEN previewPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
                    SelectObject(dc, previewPen);
                    if (draft.transformCommand == TransformCommand::Trim) {
                        const auto result = mode == EditMode::View3D
                            ? trimLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                              pick, draft.workPlane)
                            : trimLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                        if (result)
                            for (const auto& segment : *result) drawModel(segment);
                    } else {
                        const auto result = mode == EditMode::View3D
                            ? extendLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                                pick, draft.workPlane)
                            : extendLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                        if (result) drawModel(*result);
                    }
                    SelectObject(dc, stockPen);
                    DeleteObject(previewPen);
                }
            }
            if (draft.drawingActive && draft.anchor && draft.cursor) {
                // Tam kare yolundaki cizim onizlemesinin BIREBIR aynasi:
                // ortho-eksen renkli track + arac ozel onizleme (dikdortgen,
                // daire, 3DFACE) — sadece sari duz cizgi degil.
                const POINT a = projectPoint(*draft.anchor);
                const POINT b = projectPoint(*draft.cursor);
                const COLORREF previewColor = [&]() -> COLORREF {
                    switch (draft.orthoAxis) {
                    case OrthoAxis::X: return RGB(235, 82, 96);
                    case OrthoAxis::Y: return RGB(72, 211, 121);
                    case OrthoAxis::Z: return RGB(78, 148, 255);
                    default: return RGB(255, 206, 84);
                    }
                }();
                HPEN preview = CreatePen(PS_DASH, 1, previewColor);
                SelectObject(dc, preview);
                const auto drawPreviewEdges = [&](const WireframeModel& model) {
                    for (const auto& edge : model.edges()) {
                        const POINT from = projectPoint(model.vertices()[edge.from]);
                        const POINT to = projectPoint(model.vertices()[edge.to]);
                        line(dc, from.x, from.y, to.x, to.y);
                    }
                };
                if (draft.tool == DrawTool::Face3D) {
                    for (std::size_t index = 1; index < draft.facePoints.size(); ++index) {
                        const POINT from = projectPoint(draft.facePoints[index - 1]);
                        const POINT to = projectPoint(draft.facePoints[index]);
                        line(dc, from.x, from.y, to.x, to.y);
                    }
                    const POINT from = projectPoint(draft.facePoints.empty() ? *draft.anchor
                                                                             : draft.facePoints.back());
                    line(dc, from.x, from.y, b.x, b.y);
                    if (draft.facePoints.size() == 3) {
                        const POINT first = projectPoint(draft.facePoints.front());
                        line(dc, b.x, b.y, first.x, first.y);
                    }
                } else if (draft.tool == DrawTool::Rectangle) {
                    if (mode == EditMode::View3D)
                        drawPreviewEdges(WireframeModel::rectangleOnPlane(draft.workPlane,
                            draft.workPlane.toPlane(*draft.anchor), draft.workPlane.toPlane(*draft.cursor)));
                    else drawPreviewEdges(WireframeModel::rectangle(*draft.anchor, *draft.cursor));
                } else if (draft.tool == DrawTool::Circle) {
                    double radius{};
                    if (mode == EditMode::View3D) {
                        const Vec2 center = draft.workPlane.toPlane(*draft.anchor);
                        const Vec2 edge = draft.workPlane.toPlane(*draft.cursor);
                        radius = std::hypot(edge.x - center.x, edge.y - center.y);
                        if (radius > 0.0)
                            drawPreviewEdges(WireframeModel::circleOnPlane(draft.workPlane, center, radius));
                    } else {
                        radius = std::hypot(draft.cursor->x - draft.anchor->x,
                                            draft.cursor->y - draft.anchor->y);
                        if (radius > 0.0) drawPreviewEdges(WireframeModel::circle(*draft.anchor, radius));
                    }
                } else {
                    line(dc, a.x, a.y, b.x, b.y);
                }
                SelectObject(dc, stockPen);
                DeleteObject(preview);
            }
   }
            const bool trimExtendTargetSelection =
                (draft.transformCommand == TransformCommand::Trim ||
                 draft.transformCommand == TransformCommand::Extend) &&
                draft.transformPhase == TransformPhase::Destination;
            const bool neutralSelection = draft.transformCommand == TransformCommand::None &&
                                          !draft.drawingActive;
            if ((neutralSelection ||
                 (draft.transformCommand != TransformCommand::None &&
                  (draft.transformPhase == TransformPhase::Selecting || trimExtendTargetSelection))) &&
                draft.selectionFirstCorner) {
                const POINT first = *draft.selectionFirstCorner;
                const POINT second = draft.cursorScreen;
                const bool crossing = second.x < first.x;
                RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                               std::max(first.x, second.x), std::max(first.y, second.y)};
                const COLORREF color = crossing ? RGB(74, 188, 112) : RGB(54, 142, 224);
                const int selectionWidth = std::max(1L, selection.right - selection.left);
                const int selectionHeight = std::max(1L, selection.bottom - selection.top);
                HDC overlayDc = CreateCompatibleDC(dc);
                HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
                HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
                RECT overlayRect{0, 0, selectionWidth, selectionHeight};
                HBRUSH overlayBrush = CreateSolidBrush(crossing ? RGB(34, 139, 74) : RGB(36, 104, 181));
                FillRect(overlayDc, &overlayRect, overlayBrush);
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 72, 0};
                AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                           overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
                SelectObject(overlayDc, oldOverlayBitmap);
                DeleteObject(overlayBrush);
                DeleteObject(overlayBitmap);
                DeleteDC(overlayDc);
                HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                HPEN border = CreatePen(crossing ? PS_DASH : PS_SOLID, 1, color);
                HGDIOBJ oldSelectionPen = SelectObject(dc, border);
                Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
                SelectObject(dc, oldSelectionPen);
                SelectObject(dc, oldSelectionBrush);
                DeleteObject(border);
                drawText(dc, selection.left + 3, std::max(3L, selection.top - 19),
                         crossing ? L"CROSSING" : L"WINDOW", color);
            }
            if (draft.zoomWindowActive && draft.zoomWindowFirstCorner) {
                const POINT first = *draft.zoomWindowFirstCorner;
                const POINT second = draft.cursorScreen;
                RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                               std::max(first.x, second.x), std::max(first.y, second.y)};
                const int selectionWidth = std::max(1L, selection.right - selection.left);
                const int selectionHeight = std::max(1L, selection.bottom - selection.top);
                HDC overlayDc = CreateCompatibleDC(dc);
                HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
                HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
                RECT overlayRect{0, 0, selectionWidth, selectionHeight};
                HBRUSH overlayBrush = CreateSolidBrush(RGB(36, 104, 181));
                FillRect(overlayDc, &overlayRect, overlayBrush);
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 64, 0};
                AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                           overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
                SelectObject(overlayDc, oldOverlayBitmap);
                DeleteObject(overlayBrush); DeleteObject(overlayBitmap); DeleteDC(overlayDc);
                HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                HPEN border = CreatePen(PS_SOLID, 1, RGB(54, 142, 224));
                HGDIOBJ oldSelectionPen = SelectObject(dc, border);
                Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
                SelectObject(dc, oldSelectionPen); SelectObject(dc, oldSelectionBrush); DeleteObject(border);
                drawText(dc, selection.left + 3, std::max(3L, selection.top - 19), L"ZOOM", RGB(54, 142, 224));
            }
     
        if (motionDrafting && draft.cursor && draft.snapType != SnapType::None) {
            const POINT p = projectPoint(*draft.cursor);
            HPEN snapPen = CreatePen(PS_SOLID, 2, RGB(90, 255, 145));
            SelectObject(dc, snapPen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            switch (snapMarkerSymbol(draft.snapType)) {
            case SnapMarkerSymbol::Triangle: {
                POINT triangle[4]{{p.x, p.y - 7}, {p.x - 7, p.y + 6}, {p.x + 7, p.y + 6}, {p.x, p.y - 7}};
                Polyline(dc, triangle, 4);
                break;
            }
            case SnapMarkerSymbol::Circle:
                Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
                break;
            case SnapMarkerSymbol::CircleCross:
                Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
                line(dc, p.x - 4, p.y, p.x + 5, p.y); line(dc, p.x, p.y - 4, p.x, p.y + 5);
                break;
            case SnapMarkerSymbol::CrossedCircle:
                Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
                line(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
                line(dc, p.x - 6, p.y + 6, p.x + 7, p.y - 7);
                break;
            case SnapMarkerSymbol::Diamond: {
                POINT diamond[5]{{p.x, p.y - 7}, {p.x + 7, p.y}, {p.x, p.y + 7},
                                 {p.x - 7, p.y}, {p.x, p.y - 7}};
                Polyline(dc, diamond, 5);
                break;
            }
            case SnapMarkerSymbol::Cross:
                line(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
                line(dc, p.x - 6, p.y + 6, p.x + 7, p.y - 7);
                break;
            case SnapMarkerSymbol::BoxedCross:
                Rectangle(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
                line(dc, p.x - 5, p.y - 5, p.x + 6, p.y + 6);
                line(dc, p.x - 5, p.y + 5, p.x + 6, p.y - 6);
                break;
            case SnapMarkerSymbol::ExtensionLine:
                line(dc, p.x - 9, p.y, p.x - 5, p.y);
                line(dc, p.x - 2, p.y, p.x + 3, p.y);
                line(dc, p.x + 6, p.y, p.x + 10, p.y);
                break;
            case SnapMarkerSymbol::LinkedSquares:
                Rectangle(dc, p.x - 7, p.y - 7, p.x + 2, p.y + 2);
                Rectangle(dc, p.x - 1, p.y - 1, p.x + 8, p.y + 8);
                break;
            case SnapMarkerSymbol::RightAngle:
                line(dc, p.x - 7, p.y + 7, p.x - 7, p.y - 5);
                line(dc, p.x - 7, p.y - 5, p.x + 6, p.y - 5);
                line(dc, p.x - 2, p.y - 5, p.x - 2, p.y + 1);
                line(dc, p.x - 2, p.y + 1, p.x + 4, p.y + 1);
                break;
            case SnapMarkerSymbol::TangentCircle:
                Ellipse(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
                line(dc, p.x - 8, p.y - 7, p.x + 9, p.y - 7);
                break;
            case SnapMarkerSymbol::Hourglass: {
                POINT hourglass[5]{{p.x - 7, p.y - 6}, {p.x + 7, p.y - 6},
                                   {p.x - 7, p.y + 6}, {p.x + 7, p.y + 6},
                                   {p.x - 7, p.y - 6}};
                Polyline(dc, hourglass, 5);
                break;
            }
            case SnapMarkerSymbol::ParallelLines:
                line(dc, p.x - 8, p.y + 5, p.x, p.y - 5);
                line(dc, p.x, p.y + 5, p.x + 8, p.y - 5);
                break;
            case SnapMarkerSymbol::GridCross:
                line(dc, p.x - 7, p.y, p.x + 8, p.y);
                line(dc, p.x, p.y - 7, p.x, p.y + 8);
                Rectangle(dc, p.x - 3, p.y - 3, p.x + 4, p.y + 4);
                break;
            case SnapMarkerSymbol::Square:
                Rectangle(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
                break;
            case SnapMarkerSymbol::None:
                break;
            }
            SelectObject(dc, oldBrush);
            SelectObject(dc, stockPen);
            DeleteObject(snapPen);
            drawText(dc, p.x + 10, p.y + 8, snapTypeLabel(draft.snapType), RGB(90, 255, 145));
        }
        if (motionDrafting && draft.dynamicInputEnabled && draft.cursor &&
            !(draft.transformCommand != TransformCommand::None &&
              draft.transformPhase == TransformPhase::Selecting)) {
            wchar_t info[128]{};
            if (!draft.input.empty()) {
                std::swprintf(info, std::size(info), L"> %ls", draft.input.c_str());
            } else if (draft.anchor) {
                const double dx = draft.cursor->x - draft.anchor->x;
                const double dy = draft.cursor->y - draft.anchor->y;
                const double dz = draft.cursor->z - draft.anchor->z;
                const double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
                std::swprintf(info, std::size(info), L"%.3f < %.1f°  dZ %.3f",
                              std::sqrt(dx * dx + dy * dy + dz * dz), angle, dz);
            } else {
                std::swprintf(info, std::size(info), L"X %.3f  Y %.3f  Z %.3f",
                              draft.cursor->x, draft.cursor->y, draft.cursor->z);
            }
            const int boxX = std::clamp(static_cast<int>(draft.cursorScreen.x + 18),
                                        static_cast<int>(canvas.left + 4),
                                        std::max(static_cast<int>(canvas.left + 4),
                                                 static_cast<int>(canvas.right - 190)));
            const int boxY = std::clamp(static_cast<int>(draft.cursorScreen.y + 18),
                                        static_cast<int>(canvas.top + 4),
                                        std::max(static_cast<int>(canvas.top + 4),
                                                 static_cast<int>(canvas.bottom - 30)));
            RECT inputBox{boxX, boxY, boxX + 184, boxY + 25};
            HBRUSH inputBrush = CreateSolidBrush(RGB(35, 43, 58));
            FillRect(dc, &inputBox, inputBrush); DeleteObject(inputBrush);
            FrameRect(dc, &inputBox, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            drawText(dc, boxX + 7, boxY + 4, info,
                     draft.input.empty() ? RGB(221, 228, 241) : RGB(255, 216, 104));
        }
    };

    // Hızlı yol: geçerli motion tabanı varsa (kamera sabit hover hareketi)
    // temel geometriyi hiç yeniden çizme — komutlar obje sayısından bağımsızlaşır.
    if (draft.motionOverlay && !draft.snapOnly && !useGpuLines && motionBaseValid_ &&
        motionBaseWidth_ == width && motionBaseHeight_ == height) {
        if (lastMotionPath_ != 0) {
            lastMotionPath_ = 0;
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "FASTPATH\n"); fclose(diag); }
        }
        // Doğrudan hedefe: taban tek blit + geri bildirim — ara tampon ve
        // ikinci tam ekran kopyası gereksiz (taban zaten kırpışmasız zemin).
        BitBlt(target, 0, 0, width, height, motionBaseDc_, 0, 0, SRCCOPY);
        HDC overlayDc = dc;
        dc = target;
        drawMotionFeedback();
        dc = overlayDc;
        // GDI SINIRI DUZELTMESI: font satir 188'de yaratilip secilmisti; hizli
        // yol erken donuyordu ve font'u ne geri birakiyor ne de siliyordu ->
        // motion karesi basina 1 GDI nesnesi siziyor, ~10k kareden sonra
        // CreatePen NULL donup grid + cizgiler default SIYAH pen ile ciziliyordu
        // ("belli bir cizimden sonra kararma"). Font'u birak + sil.
        SelectObject(dc, oldFont);
        DeleteObject(font);
        finishPerformanceSample(false);
        return;
    }

    std::vector<std::size_t> visibleModels;
    const auto spatialQueryStart = std::chrono::steady_clock::now();
    if (mode == EditMode::Draw2D) {
        const Vec3 topLeft = camera.unproject2D({0.0, 0.0}, width, height);
        const Vec3 bottomRight = camera.unproject2D({static_cast<double>(width),
                                                     static_cast<double>(height)}, width, height);
        visibleModels = document.query2D(
            {std::min(topLeft.x, bottomRight.x), std::min(topLeft.y, bottomRight.y), -1e100},
            {std::max(topLeft.x, bottomRight.x), std::max(topLeft.y, bottomRight.y), 1e100});
    } else {
        visibleModels = document.queryBounds([&](const Bounds3& bounds) {
            return projectedBoundsIntersectsViewport(bounds, camera, width, height);
        });
    }
    performance.spatialQueryMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - spatialQueryStart).count();
    performance.visibleEntities = visibleModels.size();

    // Z-depth clipping: filter visible models to workplane range
    if (draft.depthClipEnabled && draft.resultsLoaded && draft.resultView > 0) {
        std::vector<std::size_t> clipped;
        clipped.reserve(visibleModels.size());
        for (const auto index : visibleModels) {
            if (index >= document.models().size()) continue;
            const auto& vertices = document.models()[index].vertices();
            if (vertices.size() < 2) { clipped.push_back(index); continue; }
            const Vec3 mid = (vertices.front() + vertices.back()) * 0.5;
            const Vec3 rel = mid - draft.workPlane.origin;
            const double signedDist = rel.x * draft.workPlane.normal.x +
                                      rel.y * draft.workPlane.normal.y +
                                      rel.z * draft.workPlane.normal.z;
            if (signedDist >= draft.depthClipZMin && signedDist <= draft.depthClipZMax)
                clipped.push_back(index);
        }
        visibleModels = std::move(clipped);
        performance.visibleEntities = visibleModels.size();
    }

    if (draft.interactiveNavigation) {
        if (lastMotionPath_ != 1) {
            lastMotionPath_ = 1;
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) {
                const char* reason = !motionBaseValid_ ? "NOBASE"
                    : (motionBaseWidth_ != width || motionBaseHeight_ != height)
                        ? "SIZE" : "OVERLAY";
                fprintf(diag, "FALLBACK %s %dx%d (taban %dx%d) %s%s%s%s\n", reason,
                        width, height, motionBaseWidth_, motionBaseHeight_,
                        draft.wheelNavigating ? "WHEEL " : "",
                        draft.rotating ? "ROT " : "",
                        draft.panning ? "PAN " : "",
                        draft.viewCubeActive ? "VCUBE " : "");
                fclose(diag);
            }
        }
        constexpr std::size_t interactiveModelBudget = 6'000;
        interactiveModelStride = std::max<std::size_t>(1,
            (document.models().size() + interactiveModelBudget - 1) / interactiveModelBudget);
    }
    struct ProjectedFace {
        std::vector<POINT> points;
        double depth{};
        COLORREF color{};
        COLORREF edgeColor{};
    };
    const unsigned char faceAlpha = visualStyleFaceAlpha(draft.visualStyle);
    if (!draft.snapOnly && faceAlpha != 0 && !draft.interactiveNavigation) {
        std::vector<ProjectedFace> projectedFaces;
        for (const auto index : visibleModels) {
            if (index >= document.models().size()) continue;
            const auto& model = document.models()[index];
            const auto& properties = document.effectiveProperties(index);
            if (!properties.visible) continue;
            for (const auto& face : model.faces()) {
                ProjectedFace projected;
                projected.points.reserve(face.size());
                for (const auto vertexIndex : face) {
                    const Vec3& vertex = model.vertices()[vertexIndex];
                    projected.points.push_back(projectPoint(vertex));
                    projected.depth += camera.viewTransform(vertex).z;
                }
                projected.depth /= static_cast<double>(face.size());
                projected.color = shadedFaceColor(properties.effectiveColor);
                projected.edgeColor = nativeColor(properties.effectiveColor);
                projectedFaces.push_back(std::move(projected));
            }
        }
        std::stable_sort(projectedFaces.begin(), projectedFaces.end(),
                         [](const ProjectedFace& left, const ProjectedFace& right) {
                             return left.depth < right.depth;
                         });
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        for (const auto& face : projectedFaces) {
            if (face.points.size() < 3) continue;
            if (faceAlpha == 255) {
                HBRUSH brush = CreateSolidBrush(face.color);
                HGDIOBJ oldBrush = SelectObject(dc, brush);
                HPEN borderPen = CreatePen(PS_SOLID, 1, face.edgeColor);
                HGDIOBJ previousPen = SelectObject(dc, borderPen);
                Polygon(dc, face.points.data(), static_cast<int>(face.points.size()));
                ++performance.drawCalls;
                performance.projectedVertices += face.points.size();
                SelectObject(dc, previousPen);
                SelectObject(dc, oldBrush);
                DeleteObject(borderPen);
                DeleteObject(brush);
                continue;
            }

            LONG left = face.points.front().x, right = left;
            LONG top = face.points.front().y, bottom = top;
            for (const auto& point : face.points) {
                left = std::min(left, point.x); right = std::max(right, point.x);
                top = std::min(top, point.y); bottom = std::max(bottom, point.y);
            }
            left = std::clamp<LONG>(left, 0, width);
            right = std::clamp<LONG>(right, 0, width);
            top = std::clamp<LONG>(top, 0, height);
            bottom = std::clamp<LONG>(bottom, 0, height);
            const int faceWidth = right - left;
            const int faceHeight = bottom - top;
            if (faceWidth <= 0 || faceHeight <= 0) continue;
            HDC overlayDc = CreateCompatibleDC(dc);
            HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, faceWidth, faceHeight);
            HGDIOBJ oldBitmap = SelectObject(overlayDc, overlayBitmap);
            BitBlt(overlayDc, 0, 0, faceWidth, faceHeight, dc, left, top, SRCCOPY);
            std::vector<POINT> localPoints = face.points;
            for (auto& point : localPoints) { point.x -= left; point.y -= top; }
            HBRUSH brush = CreateSolidBrush(face.color);
            HGDIOBJ oldOverlayBrush = SelectObject(overlayDc, brush);
            HGDIOBJ oldOverlayPen = SelectObject(overlayDc, GetStockObject(NULL_PEN));
            Polygon(overlayDc, localPoints.data(), static_cast<int>(localPoints.size()));
            ++performance.drawCalls;
            performance.projectedVertices += localPoints.size();
            SelectObject(overlayDc, oldOverlayPen);
            SelectObject(overlayDc, oldOverlayBrush);
            BLENDFUNCTION blend{AC_SRC_OVER, 0, faceAlpha, 0};
            AlphaBlend(dc, left, top, faceWidth, faceHeight,
                       overlayDc, 0, 0, faceWidth, faceHeight, blend);
            SelectObject(overlayDc, oldBitmap);
            DeleteObject(brush);
            DeleteObject(overlayBitmap);
            DeleteDC(overlayDc);
        }
        SelectObject(dc, oldPen);
    }

    // Phase 1 GPU-retained line rendering:
    // Collect models for GPU. Actual rendering happens AFTER GDI BitBlt
    // so lines appear on top of GDI-drawn background/grid/axis.
    std::vector<std::pair<std::size_t, mm::WireframeModel>> gpuBatch;
    if (!draft.snapOnly && useGpuLines) {
        gpuBatch.reserve(visibleModels.size());
        for (const auto index : visibleModels) {
            if (index >= document.models().size()) continue;
            const auto& model = document.models()[index];
            if (model.edges().empty()) continue;
            if (isSelected(index)) continue;
            const auto properties = document.effectiveProperties(index);
            if (!properties.visible) continue;
            auto modelCopy = model;
            modelCopy.setProperties(properties);
            gpuBatch.emplace_back(index, std::move(modelCopy));
        }
    }

    using PenKey = std::tuple<std::uint32_t, int, std::string, int>;
    std::map<PenKey, HPEN> entityPens;
    const auto entityPen = [&](const EntityProperties& properties) {
        const PenKey key{properties.effectiveColor, properties.effectiveLineWeight,
                         properties.effectiveLineType,
                         static_cast<int>(std::round(properties.lineTypeScale * 1000.0))};
        if (const auto found = entityPens.find(key); found != entityPens.end()) return found->second;
        HPEN pen = createEntityPen(properties);
        entityPens.emplace(key, pen);
        return pen;
    };
    if (!draft.snapOnly) {
    for (std::size_t visibleIndex = 0; visibleIndex < visibleModels.size(); ++visibleIndex) {
        const auto index = visibleModels[visibleIndex];
        if (index >= document.models().size() || isSelected(index)) continue;
        const auto& model = document.models()[index];
        const auto& properties = document.effectiveProperties(index);
        if (!properties.visible) continue;
        if (draft.visualStyle == VisualStyle::Solid && !draft.interactiveNavigation &&
            !model.faces().empty()) continue;
        // Phase 1 GPU: edges already rendered; skip non-selected model GDI pass
        if (useGpuLines) continue;
        if (draft.interactiveNavigation) {
            const bool representative = index % interactiveModelStride == 0 ||
                                        model.vertices().size() >= 1'000;
            if (!representative) continue;
        }
        // Ekran-boyutu LOD: zoom-out'ta minik görünen objeler tek piksel
        // işaret olarak çizilir, görünmez boyuttakiler atlanır — büyük
        // çizimlerde tam kare maliyeti ekrandaki detaya bağlı kalır.
        if (!draft.interactiveNavigation && model.faces().empty() && !model.isPointEntity()) {
            const Bounds3& bounds = document.modelBounds()[index];
            const POINT cornerA = projectPoint(bounds.minimum);
            const POINT cornerB = projectPoint(bounds.maximum);
            const double screenSize = std::max(
                std::abs(static_cast<double>(cornerB.x - cornerA.x)),
                std::abs(static_cast<double>(cornerB.y - cornerA.y)));
            if (screenSize < 0.5) { ++performance.renderedEntities; continue; }
            if (screenSize < 1.5) {
                SetPixel(dc, (cornerA.x + cornerB.x) / 2, (cornerA.y + cornerB.y) / 2,
                         properties.effectiveColor);
                ++performance.renderedEntities;
                continue;
            }
        }
        SelectObject(dc, entityPen(properties));
        drawModel(model);
        ++performance.renderedEntities;
    }
    SelectObject(dc, stockPen);
    for (const auto& [key, pen] : entityPens) {
        (void)key;
        DeleteObject(pen);
    }
    } // !draft.snapOnly
    if (draft.interactiveNavigation && !draft.snapOnly && !useGpuLines &&
        !draft.selectedModels.empty()) {
        // FALLBACK interaktif karelerde secim vurgusu: fast path disinda kalan
        // motion karelerinde yesil vurgu kaybolup 'flicker' yapiyordu. Ayni vurgu
        // tum kare tiplerinde cizilsin (b188bae deseni, motion-base oncesi).
        HPEN selectedPen = CreatePen(PS_SOLID, 3, RGB(90, 255, 145));
        SelectObject(dc, selectedPen);
        for (const auto index : draft.selectedModels) {
            if (index < document.models().size() && document.modelIsEditable(index))
                drawModel(document.models()[index]);
        }
        SelectObject(dc, stockPen);
        DeleteObject(selectedPen);
    }
    if (!draft.snapOnly && !draft.interactiveNavigation && !useGpuLines) {
        // Tam kare: temiz taban (arka plan + grid + eksenler + modeller) — henüz
        // geri bildirim katmanı çizilmedi. Motion tabanını tazele.
        ensureMotionBase(target, width, height);
        BitBlt(motionBaseDc_, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        motionBaseValid_ = true;
    }

    if (!draft.interactiveNavigation) {
        if (!draft.selectedModels.empty()) {
            HPEN selectedPen = CreatePen(PS_SOLID, 3, RGB(90, 255, 145));
            SelectObject(dc, selectedPen);
            for (const auto index : draft.selectedModels) {
                if (document.modelIsEditable(index)) {
                    drawModel(document.models()[index]);
                    ++performance.renderedEntities;
                }
            }
            SelectObject(dc, stockPen);
            DeleteObject(selectedPen);
        }

        const bool trimExtendTargetSelection =
        (draft.transformCommand == TransformCommand::Trim ||
         draft.transformCommand == TransformCommand::Extend) &&
        draft.transformPhase == TransformPhase::Destination;
    const bool neutralSelection = draft.transformCommand == TransformCommand::None &&
                                  !draft.drawingActive;
    if ((neutralSelection ||
         (draft.transformCommand != TransformCommand::None &&
          (draft.transformPhase == TransformPhase::Selecting || trimExtendTargetSelection))) &&
        draft.selectionFirstCorner) {
        const POINT first = *draft.selectionFirstCorner;
        const POINT second = draft.cursorScreen;
        const bool crossing = second.x < first.x;
        RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                       std::max(first.x, second.x), std::max(first.y, second.y)};
        const COLORREF color = crossing ? RGB(74, 188, 112) : RGB(54, 142, 224);
        const int selectionWidth = std::max(1L, selection.right - selection.left);
        const int selectionHeight = std::max(1L, selection.bottom - selection.top);
        HDC overlayDc = CreateCompatibleDC(dc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
        HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
        RECT overlayRect{0, 0, selectionWidth, selectionHeight};
        HBRUSH overlayBrush = CreateSolidBrush(crossing ? RGB(34, 139, 74) : RGB(36, 104, 181));
        FillRect(overlayDc, &overlayRect, overlayBrush);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 72, 0};
        AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                   overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
        SelectObject(overlayDc, oldOverlayBitmap);
        DeleteObject(overlayBrush);
        DeleteObject(overlayBitmap);
        DeleteDC(overlayDc);
        HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN border = CreatePen(crossing ? PS_DASH : PS_SOLID, 1, color);
        HGDIOBJ oldSelectionPen = SelectObject(dc, border);
        Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
        SelectObject(dc, oldSelectionPen);
        SelectObject(dc, oldSelectionBrush);
        DeleteObject(border);
        drawText(dc, selection.left + 3, std::max(3L, selection.top - 19),
                 crossing ? L"CROSSING" : L"WINDOW", color);
    }

    // Draw node window selection rectangle
    if (draft.nodeConstraintsVisible && draft.nodeSelectionFirstCorner) {
        const POINT first = *draft.nodeSelectionFirstCorner;
        const POINT second = draft.cursorScreen;
        RECT sel{std::min(first.x, second.x), std::min(first.y, second.y),
                 std::max(first.x, second.x), std::max(first.y, second.y)};
        HPEN border = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, sel.left, sel.top, sel.right, sel.bottom);
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
        DeleteObject(border);
    }

    if (draft.zoomWindowActive && draft.zoomWindowFirstCorner) {
        const POINT first = *draft.zoomWindowFirstCorner;
        const POINT second = draft.cursorScreen;
        RECT selection{std::min(first.x, second.x), std::min(first.y, second.y),
                       std::max(first.x, second.x), std::max(first.y, second.y)};
        const int selectionWidth = std::max(1L, selection.right - selection.left);
        const int selectionHeight = std::max(1L, selection.bottom - selection.top);
        HDC overlayDc = CreateCompatibleDC(dc);
        HBITMAP overlayBitmap = CreateCompatibleBitmap(dc, selectionWidth, selectionHeight);
        HGDIOBJ oldOverlayBitmap = SelectObject(overlayDc, overlayBitmap);
        RECT overlayRect{0, 0, selectionWidth, selectionHeight};
        HBRUSH overlayBrush = CreateSolidBrush(RGB(36, 104, 181));
        FillRect(overlayDc, &overlayRect, overlayBrush);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 64, 0};
        AlphaBlend(dc, selection.left, selection.top, selectionWidth, selectionHeight,
                   overlayDc, 0, 0, selectionWidth, selectionHeight, blend);
        SelectObject(overlayDc, oldOverlayBitmap);
        DeleteObject(overlayBrush); DeleteObject(overlayBitmap); DeleteDC(overlayDc);
        HGDIOBJ oldSelectionBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN border = CreatePen(PS_SOLID, 1, RGB(54, 142, 224));
        HGDIOBJ oldSelectionPen = SelectObject(dc, border);
        Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
        SelectObject(dc, oldSelectionPen); SelectObject(dc, oldSelectionBrush); DeleteObject(border);
        drawText(dc, selection.left + 3, std::max(3L, selection.top - 19),
                 L"ZOOM WINDOW", RGB(54, 142, 224));
    }

    if (draft.transformCommand != TransformCommand::None &&
        draft.transformPhase == TransformPhase::Destination && draft.transformBase && draft.cursor) {
        const POINT basePoint = projectPoint(*draft.transformBase);
        const POINT destinationPoint = projectPoint(*draft.cursor);
        const COLORREF trackerColor = [&]() -> COLORREF {
            switch (draft.orthoAxis) {
            case OrthoAxis::X: return RGB(235, 82, 96);   // Red
            case OrthoAxis::Y: return RGB(72, 211, 121);  // Green
            case OrthoAxis::Z: return RGB(78, 148, 255);  // Blue
            default: return RGB(255, 206, 84);            // Yellow (no ortho)
            }
        }();
        HPEN trackerPen = CreatePen(PS_DOT, 1, trackerColor);
        SelectObject(dc, trackerPen);
        line(dc, basePoint.x, basePoint.y, destinationPoint.x, destinationPoint.y);
        SelectObject(dc, stockPen);
        DeleteObject(trackerPen);

        HPEN transformPreview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
        SelectObject(dc, transformPreview);
        for (const auto index : draft.selectedModels) {
            if (index >= document.models().size()) continue;
            if (draft.transformCommand == TransformCommand::Mirror) {
                const auto preview = mode == EditMode::View3D
                    ? mirrorModelOnPlane(document.models()[index], *draft.transformBase,
                                         *draft.cursor, draft.workPlane)
                    : mirrorModel2D(document.models()[index], *draft.transformBase, *draft.cursor);
                if (preview) drawModel(*preview);
            } else if (draft.transformCommand == TransformCommand::LinearArray && draft.arrayItemCount) {
                for (const auto& preview : linearArray2D(document.models()[index], *draft.arrayItemCount,
                                                         *draft.cursor - *draft.transformBase))
                    drawModel(preview);
            } else if (draft.transformCommand == TransformCommand::Rotate) {
                constexpr double pi = 3.14159265358979323846;
                const double angleDeg = std::atan2(draft.cursor->y - draft.transformBase->y,
                                                   draft.cursor->x - draft.transformBase->x) * 180.0 / pi;
                const auto preview = mode == EditMode::View3D
                    ? rotateModelOnPlane(document.models()[index], *draft.transformBase, angleDeg, draft.workPlane)
                    : rotateModel2D(document.models()[index], *draft.transformBase, angleDeg);
                if (preview) drawModel(*preview);
            } else if (draft.transformCommand == TransformCommand::Rotate3D && draft.rotateAxis) {
                constexpr double pi = 3.14159265358979323846;
                const Vec3 axisDir = *draft.rotateAxis - *draft.transformBase;
                if (axisDir.x * axisDir.x + axisDir.y * axisDir.y + axisDir.z * axisDir.z > 1e-12) {
                    const double angleDeg = std::atan2(draft.cursor->y - draft.transformBase->y,
                                                       draft.cursor->x - draft.transformBase->x) * 180.0 / pi;
                    const auto preview = rotateModelAroundAxis(document.models()[index],
                                                               *draft.transformBase, axisDir, angleDeg);
                    if (preview) drawModel(*preview);
                }
            } else {
                drawModel(document.models()[index], *draft.cursor - *draft.transformBase);
            }
        }
        SelectObject(dc, stockPen);
        DeleteObject(transformPreview);
    }

    // Draw node constraints — always visible when they exist
    if (draft.nodeConstraints && !draft.nodeConstraints->empty()) {
        for (const auto& [key, constraint] : *draft.nodeConstraints) {
            Vec3 pt{};
            if (std::sscanf(key.c_str(), "%lf,%lf,%lf", &pt.x, &pt.y, &pt.z) != 3) continue;
            const auto projected = projectPoint(pt);
            const int px = projected.x;
            const int py = projected.y;
            if (px < 0 || px > client.right || py < 0 || py > client.bottom) continue;
            const bool selected = draft.selectedNodeKeys && draft.selectedNodeKeys->count(key);
            COLORREF color;
            int radius;
            if (selected) { color = RGB(255, 206, 84); radius = 7; }
            else if (constraint.isFixed()) { color = RGB(235, 82, 96); radius = 6; }
            else if (constraint.isPinned()) { color = RGB(72, 211, 121); radius = 5; }
            else { color = RGB(78, 148, 255); radius = 4; }
            HPEN nodePen = CreatePen(PS_SOLID, selected ? 3 : 2, color);
            HBRUSH nodeBrush = selected ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(color);
            const HGDIOBJ oldNodePen = SelectObject(dc, nodePen);
            const HGDIOBJ oldNodeBrush = SelectObject(dc, nodeBrush);
            Ellipse(dc, px - radius, py - radius, px + radius, py + radius);
            // Once eski nesneleri geri sec, SONRA sil — DC'ye secili nesne
            // silinirse DeleteObject sessizce basarisiz olur ve GDI nesnesi
            // sizar (node basina frame basina 1 pen + 1 brush).
            SelectObject(dc, oldNodeBrush);
            SelectObject(dc, oldNodePen);
            DeleteObject(nodePen);
            if (!selected) DeleteObject(nodeBrush);
        }
    }

    // Draw beam load arrows
    if (draft.beamLoads && !draft.beamLoads->empty()) {
        for (const auto& [modelIdx, load] : *draft.beamLoads) {
            if (load.wY == 0.0 && load.wZ == 0.0) continue;
            if (modelIdx >= document.models().size()) continue;
            const auto& m = document.models()[modelIdx];
            if (m.vertices().size() != 2) continue;
            const POINT p1 = projectPoint(m.vertices()[0]);
            const POINT p2 = projectPoint(m.vertices()[1]);
            const double len = std::hypot(p2.x - p1.x, p2.y - p1.y);
            if (len < 1.0) continue;
            const int steps = std::max(1, static_cast<int>(len / 25.0));
            const double w = load.wY + load.wZ; // total load magnitude for arrow size
            const double arrowLen = std::clamp(std::abs(w) * 3.0, 6.0, 20.0);
            HPEN loadPen = CreatePen(PS_SOLID, 2, w > 0 ? RGB(235, 82, 96) : RGB(72, 211, 121));
            const HGDIOBJ oldLoadPen = SelectObject(dc, loadPen);
            for (int i = 0; i <= steps; ++i) {
                const double t = (steps == 0) ? 0.5 : static_cast<double>(i) / steps;
                const int cx = static_cast<int>(p1.x + (p2.x - p1.x) * t);
                const int cy = static_cast<int>(p1.y + (p2.y - p1.y) * t);
                MoveToEx(dc, cx, cy, NULL);
                LineTo(dc, cx, cy + static_cast<int>(arrowLen));
            }
            SelectObject(dc, oldLoadPen);
            DeleteObject(loadPen);
        }
    }

    if (draft.transformCommand == TransformCommand::PolarArray &&
        draft.transformPhase == TransformPhase::BasePoint && draft.arrayItemCount && draft.cursor) {
        HPEN previewPen = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
        SelectObject(dc, previewPen);
        for (const auto index : draft.selectedModels) {
            if (index >= document.models().size()) continue;
            const auto previews = mode == EditMode::View3D
                ? polarArrayOnPlane(document.models()[index], *draft.arrayItemCount,
                                    *draft.cursor, draft.workPlane)
                : polarArray2D(document.models()[index], *draft.arrayItemCount, *draft.cursor);
            for (const auto& preview : previews)
                drawModel(preview);
        }
        SelectObject(dc, stockPen);
        DeleteObject(previewPen);
    }

    if ((draft.transformCommand == TransformCommand::Trim ||
         draft.transformCommand == TransformCommand::Extend) &&
        draft.transformPhase == TransformPhase::Destination && !draft.modifierBoundaries.empty() &&
        !draft.trimExtendPreviewSuppressed) {
        // Sınır çizgileri yeşil vurgu ile üstten çizilmez: vurgu, kesilen
        // bölgeden geçtiği için sonuç "eski çizgi hâlâ duruyor" gibi
        // okunuyordu. Sınırlar zaten temel geçişte kendi rengiyle çiziliyor.

        Vec3 pick{};
        std::optional<std::size_t> target;
        if (mode == EditMode::Draw2D) {
            pick = camera.unproject2D({static_cast<double>(draft.cursorScreen.x),
                                       static_cast<double>(draft.cursorScreen.y)}, width, height);
            target = hitTestModel2D(pick, document, 10.0 / (60.0 * camera.zoom()));
        } else {
            WorkPlane targetPlane = draft.workPlane;
            if (!draft.modifierBoundaries.front().vertices().empty())
                targetPlane.origin = draft.modifierBoundaries.front().vertices().front();
            if (const auto projectedPick = camera.unprojectToPlane(
                    {static_cast<double>(draft.cursorScreen.x), static_cast<double>(draft.cursorScreen.y)},
                    width, height, targetPlane))
                pick = *projectedPick;
            target = hitTestModel3D({static_cast<double>(draft.cursorScreen.x),
                                     static_cast<double>(draft.cursorScreen.y)},
                                    document, camera, width, height, 10.0);
        }
        if (target && *target < document.models().size()) {
            HPEN previewPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
            SelectObject(dc, previewPen);
            if (draft.transformCommand == TransformCommand::Trim) {
                const auto result = mode == EditMode::View3D
                    ? trimLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                      pick, draft.workPlane)
                    : trimLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                if (result)
                    for (const auto& segment : *result) drawModel(segment);
            } else {
                const auto result = mode == EditMode::View3D
                    ? extendLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                        pick, draft.workPlane)
                    : extendLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                if (result) drawModel(*result);
            }
            SelectObject(dc, stockPen);
            DeleteObject(previewPen);
        }
    }

    if (draft.transformCommand == TransformCommand::Fillet && draft.filletFirstPick &&
        draft.selectedModels.size() == 1 && draft.selectedModels.front() < document.models().size()) {
        std::optional<Vec3> pick;
        std::optional<std::size_t> target;
        if (mode == EditMode::Draw2D) {
            pick = camera.unproject2D({static_cast<double>(draft.cursorScreen.x),
                                       static_cast<double>(draft.cursorScreen.y)}, width, height);
            target = hitTestModel2D(*pick, document, 10.0 / (60.0 * camera.zoom()));
        } else {
            pick = camera.unprojectToPlane({static_cast<double>(draft.cursorScreen.x),
                                            static_cast<double>(draft.cursorScreen.y)},
                                           width, height, draft.workPlane);
            target = hitTestModel3D({static_cast<double>(draft.cursorScreen.x),
                                     static_cast<double>(draft.cursorScreen.y)},
                                    document, camera, width, height, 10.0);
        }
        if (pick && target && *target != draft.selectedModels.front() &&
            *target < document.models().size()) {
            const auto result = filletLinesOnPlane(
                document.models()[draft.selectedModels.front()], *draft.filletFirstPick,
                document.models()[*target], *pick, draft.filletRadius, draft.workPlane);
            if (result) {
                HPEN previewPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
                SelectObject(dc, previewPen);
                drawModel(result->first);
                drawModel(result->second);
                drawModel(result->arc);
                SelectObject(dc, stockPen);
                DeleteObject(previewPen);
            }
        }
    }

    if (draft.transformCommand == TransformCommand::Offset &&
        draft.transformPhase == TransformPhase::Destination && draft.offsetDistance && draft.cursor &&
        draft.selectedModels.size() == 1 && draft.selectedModels.front() < document.models().size()) {
        const auto preview = mode == EditMode::View3D
            ? offsetModelOnPlane(document.models()[draft.selectedModels.front()],
                                 *draft.offsetDistance, *draft.cursor, draft.workPlane)
            : offsetModel2D(document.models()[draft.selectedModels.front()],
                            *draft.offsetDistance, *draft.cursor);
        if (preview) {
            HPEN previewPen = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
            SelectObject(dc, previewPen);
            drawModel(*preview);
            SelectObject(dc, stockPen);
            DeleteObject(previewPen);
        }
    }

    if (draft.workPlanePicking) {
        HPEN pickPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
        SelectObject(dc, pickPen);
        for (std::size_t index = 1; index < draft.workPlanePoints.size(); ++index) {
            const POINT a = projectPoint(draft.workPlanePoints[index - 1]);
            const POINT b = projectPoint(draft.workPlanePoints[index]);
            line(dc, a.x, a.y, b.x, b.y);
        }
        if (!draft.workPlanePoints.empty() && draft.cursor) {
            const POINT a = projectPoint(draft.workPlanePoints.back());
            const POINT b = projectPoint(*draft.cursor);
            line(dc, a.x, a.y, b.x, b.y);
        }
        SelectObject(dc, stockPen);
        DeleteObject(pickPen);
        for (std::size_t index = 0; index < draft.workPlanePoints.size(); ++index) {
            const POINT point = projectPoint(draft.workPlanePoints[index]);
            HBRUSH marker = CreateSolidBrush(RGB(255, 206, 84));
            HGDIOBJ oldMarker = SelectObject(dc, marker);
            Ellipse(dc, point.x - 5, point.y - 5, point.x + 6, point.y + 6);
            SelectObject(dc, oldMarker);
            DeleteObject(marker);
            const std::wstring label = std::to_wstring(index + 1);
            drawText(dc, point.x + 8, point.y - 14, label.c_str(), RGB(255, 206, 84));
        }
    }

    const bool drafting = commandShowsSnapFeedback(
        draft.drawingActive, draft.workPlanePicking, draft.transformCommand, draft.transformPhase,
        draft.arrayItemCount.has_value(), draft.offsetDistance.has_value());

    if (drafting && draft.polarTrackingEnabled && !draft.temporaryTrackingPoints.empty()) {
        const COLORREF trackingColor = RGB(80, 225, 255);
        HPEN trackingPen = CreatePen(PS_DOT, 1, trackingColor);
        SelectObject(dc, trackingPen);
        for (const auto& guide : draft.temporaryTrackingGuides) {
            const POINT from = projectPoint(guide.from);
            const POINT to = projectPoint(guide.to);
            line(dc, from.x, from.y, to.x, to.y);
        }
        SelectObject(dc, stockPen);
        DeleteObject(trackingPen);

        HPEN tempPen = CreatePen(PS_SOLID, 2, RGB(255, 110, 220));
        SelectObject(dc, tempPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (std::size_t index = 0; index < draft.temporaryTrackingPoints.size(); ++index) {
            const POINT point = projectPoint(draft.temporaryTrackingPoints[index]);
            line(dc, point.x - 5, point.y - 5, point.x + 5, point.y + 5);
            line(dc, point.x - 5, point.y + 5, point.x + 5, point.y - 5);
            Ellipse(dc, point.x - 7, point.y - 7, point.x + 8, point.y + 8);
            const std::wstring label = L"TP" + std::to_wstring(index + 1);
            drawText(dc, point.x + 9, point.y - 18, label.c_str(), RGB(255, 110, 220));
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, stockPen);
        DeleteObject(tempPen);

        HPEN derivedPen = CreatePen(PS_SOLID, 1, trackingColor);
        SelectObject(dc, derivedPen);
        for (const auto& derived : draft.temporaryDerivedPoints) {
            const POINT point = projectPoint(derived);
            POINT diamond[5]{{point.x, point.y - 5}, {point.x + 5, point.y},
                             {point.x, point.y + 5}, {point.x - 5, point.y},
                             {point.x, point.y - 5}};
            Polyline(dc, diamond, 5);
        }
        SelectObject(dc, stockPen);
        DeleteObject(derivedPen);
    }

    if (draft.polarTrackingEnabled && draft.polarTrackingLocked &&
        !draft.temporaryTrackingLocked && draft.cursor) {
        std::optional<Vec3> polarAnchor = draft.transformPhase == TransformPhase::Destination
            ? draft.transformBase : draft.anchor;
        if (!polarAnchor && draft.workPlanePicking && !draft.workPlanePoints.empty())
            polarAnchor = draft.workPlanePoints.back();
        if (polarAnchor) {
            const POINT from = projectPoint(*polarAnchor);
            const POINT through = projectPoint(*draft.cursor);
            const double dx = static_cast<double>(through.x - from.x);
            const double dy = static_cast<double>(through.y - from.y);
            const double length = std::hypot(dx, dy);
            if (length > 0.5) {
                const double extension = static_cast<double>(std::max(width, height));
                const POINT to{through.x + static_cast<LONG>(dx * extension / length),
                               through.y + static_cast<LONG>(dy * extension / length)};
                HPEN polarPen = CreatePen(PS_DOT, 1, RGB(80, 225, 255));
                SelectObject(dc, polarPen);
                line(dc, from.x, from.y, to.x, to.y);
                SelectObject(dc, stockPen);
                DeleteObject(polarPen);
                drawText(dc, through.x + 12, through.y - 22, L"POLAR 90°", RGB(80, 225, 255));
            }
        }
    }

    if (drafting && draft.anchor && draft.cursor) {
        const POINT a = projectPoint(*draft.anchor);
        const POINT b = projectPoint(*draft.cursor);
        const COLORREF previewColor = [&]() -> COLORREF {
            switch (draft.orthoAxis) {
            case OrthoAxis::X: return RGB(235, 82, 96);
            case OrthoAxis::Y: return RGB(72, 211, 121);
            case OrthoAxis::Z: return RGB(78, 148, 255);
            default: return RGB(255, 206, 84);
            }
        }();
        HPEN preview = CreatePen(PS_DASH, 1, previewColor);
        SelectObject(dc, preview);
        const auto drawPreviewModel = [&](const WireframeModel& model) {
            for (const auto& edge : model.edges()) {
                const POINT from = projectPoint(model.vertices()[edge.from]);
                const POINT to = projectPoint(model.vertices()[edge.to]);
                line(dc, from.x, from.y, to.x, to.y);
            }
        };
        if (draft.tool == DrawTool::Face3D) {
            for (std::size_t index = 1; index < draft.facePoints.size(); ++index) {
                const POINT from = projectPoint(draft.facePoints[index - 1]);
                const POINT to = projectPoint(draft.facePoints[index]);
                line(dc, from.x, from.y, to.x, to.y);
            }
            const POINT from = projectPoint(draft.facePoints.empty() ? *draft.anchor
                                                                     : draft.facePoints.back());
            line(dc, from.x, from.y, b.x, b.y);
            if (draft.facePoints.size() == 3) {
                const POINT first = projectPoint(draft.facePoints.front());
                line(dc, b.x, b.y, first.x, first.y);
            }
        } else if (draft.tool == DrawTool::Rectangle) {
            if (mode == EditMode::View3D)
                drawPreviewModel(WireframeModel::rectangleOnPlane(draft.workPlane,
                    draft.workPlane.toPlane(*draft.anchor), draft.workPlane.toPlane(*draft.cursor)));
            else drawPreviewModel(WireframeModel::rectangle(*draft.anchor, *draft.cursor));
        } else if (draft.tool == DrawTool::Circle) {
            double radius{};
            if (mode == EditMode::View3D) {
                const Vec2 center = draft.workPlane.toPlane(*draft.anchor);
                const Vec2 edge = draft.workPlane.toPlane(*draft.cursor);
                radius = std::hypot(edge.x - center.x, edge.y - center.y);
                if (radius > 0.0)
                    drawPreviewModel(WireframeModel::circleOnPlane(draft.workPlane, center, radius));
            } else {
                radius = std::hypot(draft.cursor->x - draft.anchor->x,
                                    draft.cursor->y - draft.anchor->y);
                if (radius > 0.0) drawPreviewModel(WireframeModel::circle(*draft.anchor, radius));
            }
        } else {
            line(dc, a.x, a.y, b.x, b.y);
        }
        SelectObject(dc, stockPen);
        DeleteObject(preview);
    }

    if (drafting && draft.cursor && draft.snapType != SnapType::None) {
        const POINT p = projectPoint(*draft.cursor);
        HPEN snapPen = CreatePen(PS_SOLID, 2, RGB(90, 255, 145));
        SelectObject(dc, snapPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        switch (snapMarkerSymbol(draft.snapType)) {
        case SnapMarkerSymbol::Triangle: {
            POINT triangle[4]{{p.x, p.y - 7}, {p.x - 7, p.y + 6}, {p.x + 7, p.y + 6}, {p.x, p.y - 7}};
            Polyline(dc, triangle, 4);
            break;
        }
        case SnapMarkerSymbol::Circle:
            Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
            break;
        case SnapMarkerSymbol::CircleCross:
            Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
            line(dc, p.x - 4, p.y, p.x + 5, p.y); line(dc, p.x, p.y - 4, p.x, p.y + 5);
            break;
        case SnapMarkerSymbol::CrossedCircle:
            Ellipse(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
            line(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            line(dc, p.x - 6, p.y + 6, p.x + 7, p.y - 7);
            break;
        case SnapMarkerSymbol::Diamond: {
            POINT diamond[5]{{p.x, p.y - 7}, {p.x + 7, p.y}, {p.x, p.y + 7},
                             {p.x - 7, p.y}, {p.x, p.y - 7}};
            Polyline(dc, diamond, 5);
            break;
        }
        case SnapMarkerSymbol::Cross:
            line(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            line(dc, p.x - 6, p.y + 6, p.x + 7, p.y - 7);
            break;
        case SnapMarkerSymbol::BoxedCross:
            Rectangle(dc, p.x - 7, p.y - 7, p.x + 8, p.y + 8);
            line(dc, p.x - 5, p.y - 5, p.x + 6, p.y + 6);
            line(dc, p.x - 5, p.y + 5, p.x + 6, p.y - 6);
            break;
        case SnapMarkerSymbol::ExtensionLine:
            line(dc, p.x - 9, p.y, p.x - 5, p.y);
            line(dc, p.x - 2, p.y, p.x + 3, p.y);
            line(dc, p.x + 6, p.y, p.x + 10, p.y);
            break;
        case SnapMarkerSymbol::LinkedSquares:
            Rectangle(dc, p.x - 7, p.y - 7, p.x + 2, p.y + 2);
            Rectangle(dc, p.x - 1, p.y - 1, p.x + 8, p.y + 8);
            break;
        case SnapMarkerSymbol::RightAngle:
            line(dc, p.x - 7, p.y + 7, p.x - 7, p.y - 5);
            line(dc, p.x - 7, p.y - 5, p.x + 6, p.y - 5);
            line(dc, p.x - 2, p.y - 5, p.x - 2, p.y + 1);
            line(dc, p.x - 2, p.y + 1, p.x + 4, p.y + 1);
            break;
        case SnapMarkerSymbol::TangentCircle:
            Ellipse(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            line(dc, p.x - 8, p.y - 7, p.x + 9, p.y - 7);
            break;
        case SnapMarkerSymbol::Hourglass: {
            POINT hourglass[5]{{p.x - 7, p.y - 6}, {p.x + 7, p.y - 6},
                               {p.x - 7, p.y + 6}, {p.x + 7, p.y + 6},
                               {p.x - 7, p.y - 6}};
            Polyline(dc, hourglass, 5);
            break;
        }
        case SnapMarkerSymbol::ParallelLines:
            line(dc, p.x - 8, p.y + 5, p.x, p.y - 5);
            line(dc, p.x, p.y + 5, p.x + 8, p.y - 5);
            break;
        case SnapMarkerSymbol::GridCross:
            line(dc, p.x - 7, p.y, p.x + 8, p.y);
            line(dc, p.x, p.y - 7, p.x, p.y + 8);
            Rectangle(dc, p.x - 3, p.y - 3, p.x + 4, p.y + 4);
            break;
        case SnapMarkerSymbol::Square:
            Rectangle(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
            break;
        case SnapMarkerSymbol::None:
            break;
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, stockPen);
        DeleteObject(snapPen);
        drawText(dc, p.x + 10, p.y + 8, snapTypeLabel(draft.snapType), RGB(90, 255, 145));
    }

    if (!draft.snapOnly) {

    if (drafting && draft.dynamicInputEnabled && draft.cursor &&
        !(draft.transformCommand != TransformCommand::None && draft.transformPhase == TransformPhase::Selecting)) {
        wchar_t info[128]{};
        if (!draft.input.empty()) {
            std::swprintf(info, std::size(info), L"> %ls", draft.input.c_str());
        } else if (draft.anchor) {
            const double dx = draft.cursor->x - draft.anchor->x;
            const double dy = draft.cursor->y - draft.anchor->y;
            const double dz = draft.cursor->z - draft.anchor->z;
            const double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            std::swprintf(info, std::size(info), L"%.3f < %.1f°  dZ %.3f",
                          std::sqrt(dx * dx + dy * dy + dz * dz), angle, dz);
        } else {
            std::swprintf(info, std::size(info), L"X %.3f  Y %.3f  Z %.3f",
                          draft.cursor->x, draft.cursor->y, draft.cursor->z);
        }
        const int boxX = std::clamp(static_cast<int>(draft.cursorScreen.x + 18), static_cast<int>(canvas.left + 4),
                                    std::max(static_cast<int>(canvas.left + 4), static_cast<int>(canvas.right - 190)));
        const int boxY = std::clamp(static_cast<int>(draft.cursorScreen.y + 18), static_cast<int>(canvas.top + 4),
                                    std::max(static_cast<int>(canvas.top + 4), static_cast<int>(canvas.bottom - 30)));
        RECT inputBox{boxX, boxY, boxX + 184, boxY + 25};
        HBRUSH inputBrush = CreateSolidBrush(RGB(35, 43, 58));
        FillRect(dc, &inputBox, inputBrush); DeleteObject(inputBrush);
        FrameRect(dc, &inputBox, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        drawText(dc, boxX + 7, boxY + 4, info, draft.input.empty() ? RGB(221, 228, 241) : RGB(255, 216, 104));
    }


    if (draft.resultsLoaded && draft.resultView > 0 && draft.nodeDisplacements && draft.elementForces &&
        draft.nodeDisplacements->size() > 0 && draft.resultView <= 6) {
        const auto& nodeDisp = *draft.nodeDisplacements;
        const auto& elemForce = *draft.elementForces;
        const double userScale = draft.resultScale;

        // Determine force type index: 1=Deformed, 2=My, 3=Mz, 4=N, 5=Vy, 6=Vz
        int forceOffset = 0; // 0=N, 1=Vy, 2=Vz, 4=Mz, 5=My (localForce order)
        if (draft.resultView == 2) forceOffset = 5; // My
        else if (draft.resultView == 3) forceOffset = 4; // Mz
        else if (draft.resultView == 4) forceOffset = 0; // N
        else if (draft.resultView == 5) forceOffset = 1; // Vy
        else if (draft.resultView == 6) forceOffset = 2; // Vz

        // First pass: find max absolute value for normalization
        double maxVal = 0.0;
        std::vector<std::pair<double,double>> elemValues;
        elemValues.reserve(visibleModels.size());
        std::vector<POINT> elemScreen[2]; // just reuse later
        for (const auto index : visibleModels) {
            if (index >= document.models().size()) { elemValues.push_back({0,0}); continue; }
            const auto& model = document.models()[index];
            const auto& vertices = model.vertices();
            if (vertices.size() != 2 || draft.resultView == 1) { elemValues.push_back({0,0}); continue; }
            const std::size_t fi = index < elemForce.size() / 12 ? index * 12 : static_cast<std::size_t>(-1);
            if (fi >= elemForce.size()) { elemValues.push_back({0,0}); continue; }
            double f1 = elemForce[fi + forceOffset];
            double f2 = elemForce[fi + forceOffset + 6];
            elemValues.push_back({f1, f2});
            maxVal = std::max(maxVal, std::max(std::abs(f1), std::abs(f2)));
        }
        if (maxVal < 1e-30) {
            // no significant forces — skip drawing (no SelectObject needed since we haven't selected)
        } else {
            const COLORREF baseColor =
                draft.resultView == 1 ? RGB(90,255,145) :
                draft.resultView == 2 || draft.resultView == 3 ? RGB(235,82,96) :
                draft.resultView == 4 ? RGB(78,148,255) :
                RGB(255,206,84);
            const COLORREF fillColor =
                draft.resultView == 1 ? RGB(36,102,58) :
                draft.resultView == 2 || draft.resultView == 3 ? RGB(94,33,38) :
                draft.resultView == 4 ? RGB(31,59,102) :
                RGB(102,82,34);
            HPEN resultPen = CreatePen(PS_SOLID, 2, baseColor);
            SelectObject(dc, resultPen);
            // Scale factor: normalize so max = ~40 pixels offset
            const double pixelScale = 40.0 / maxVal * userScale;

            for (std::size_t vi = 0; vi < visibleModels.size(); ++vi) {
                const auto index = visibleModels[vi];
                if (index >= document.models().size()) continue;
                const auto& model = document.models()[index];
                const auto& vertices = model.vertices();
                if (vertices.size() < 2) continue;

                // Z-depth clipping relative to workplane
                if (draft.depthClipEnabled) {
                    const Vec3 mid = (vertices.front() + vertices.back()) * 0.5;
                    const Vec3 rel = mid - draft.workPlane.origin;
                    const double signedDist = rel.x * draft.workPlane.normal.x +
                                              rel.y * draft.workPlane.normal.y +
                                              rel.z * draft.workPlane.normal.z;
                    if (signedDist < draft.depthClipZMin || signedDist > draft.depthClipZMax)
                        continue;
                }

                if (draft.resultView == 1) {
                    // Deformed shape — still line-based for clarity
                    Vec3 pt0 = vertices[0];
                    if (vi < nodeDisp.size()) pt0 = pt0 + nodeDisp[vi] * (userScale * 100.0);
                    const POINT from = projectPoint(pt0);
                    for (std::size_t vi2 = 1; vi2 < vertices.size(); ++vi2) {
                        Vec3 pt = vertices[vi2];
                        if (vi2 < nodeDisp.size()) pt = pt + nodeDisp[vi2] * (userScale * 100.0);
                        const POINT to = projectPoint(pt);
                        line(dc, from.x, from.y, to.x, to.y);
                    }
                    continue;
                }

                const auto [f1, f2] = vi < elemValues.size() ? elemValues[vi] : std::pair{0.0, 0.0};
                if (vertices.size() != 2) continue;

                const POINT p1 = projectPoint(vertices[0]);
                const POINT p2 = projectPoint(vertices[1]);
                const double lenX = static_cast<double>(p2.x - p1.x);
                const double lenY = static_cast<double>(p2.y - p1.y);
                const double length = std::hypot(lenX, lenY);
                if (length < 2.0) continue;
                const double ux = lenX / length, uy = lenY / length;
                const double px = -uy, py = ux; // perpendicular

                const double off1 = f1 * pixelScale;
                const double off2 = f2 * pixelScale;
                const POINT q1{static_cast<LONG>(p1.x + px * off1), static_cast<LONG>(p1.y + py * off1)};
                const POINT q2{static_cast<LONG>(p2.x + px * off2), static_cast<LONG>(p2.y + py * off2)};

                // filled polygon: original line + diagram line
                POINT poly[4] = {p1, p2, q2, q1};
                HBRUSH fillBrush = CreateSolidBrush(fillColor);
                HGDIOBJ oldBrush = SelectObject(dc, fillBrush);
                Polygon(dc, poly, 4);
                SelectObject(dc, oldBrush);
                DeleteObject(fillBrush);
                // border on diagram side only
                line(dc, q1.x, q1.y, q2.x, q2.y);
            }
            SelectObject(dc, stockPen);
            DeleteObject(resultPen);
        }
    }

    if (draft.performanceOverlayEnabled) {
        const auto& stats = performanceTracker_.latest();
        RECT panel{12, 12, 302, 222};
        HBRUSH panelBrush = CreateSolidBrush(RGB(20, 27, 37));
        FillRect(dc, &panel, panelBrush);
        DeleteObject(panelBrush);
        HPEN panelPen = CreatePen(PS_SOLID, 1, RGB(74, 91, 112));
        HGDIOBJ oldPanelPen = SelectObject(dc, panelPen);
        HGDIOBJ oldPanelBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, panel.left, panel.top, panel.right, panel.bottom);
        SelectObject(dc, oldPanelBrush);
        SelectObject(dc, oldPanelPen);
        DeleteObject(panelPen);

        drawText(dc, 24, 22, L"PERFORMANCE  [F11]", RGB(90, 205, 255));
        wchar_t value[128]{};
        int y = 46;
        const auto metric = [&](const wchar_t* format, auto... arguments) {
            std::swprintf(value, std::size(value), format, arguments...);
            drawText(dc, 24, y, value, RGB(225, 232, 241));
            y += 19;
        };
        metric(L"Paint FPS: %.1f", stats.framesPerSecond);
        metric(L"CPU Frame: %.3f ms", stats.cpuFrameMilliseconds);
        metric(L"Spatial Query: %.3f ms", stats.spatialQueryMilliseconds);
        metric(L"Entities: %zu total / %zu visible", stats.totalEntities, stats.visibleEntities);
        metric(L"Rendered: %zu   Culled: %zu", stats.renderedEntities, stats.culledEntities);
        metric(L"GDI Geometry Calls: %zu", stats.drawCalls);
        metric(L"Projected Vertices: %zu", stats.projectedVertices);
        metric(L"Tracked Buffer Growths: %zu", stats.frameBufferGrowths);
        if (glBackend && glBackend->isHardwareAccelerated()) {
            metric(L"GL Draw Calls: %zu   Lines: %zu", glBackend->drawCallsPerFrame(),
                   glBackend->renderedLineCount());
            metric(L"GL Upload: %zu KB   Rebuilds: %zu",
                   glBackend->bufferUploadBytes() / 1024,
                   glBackend->bufferRebuildCount());
        } else {
            drawText(dc, 24, y, L"GPU Frame / Upload: N/A (GDI)", RGB(160, 174, 191));
        }
    }
    }
    } // !draft.snapOnly (dynamic input + perf overlay)

    // İnteraktif karelerde komut geri bildirimi: büyük çizimlerde temel
    // geometri seyrek/kaba çizilirken seçim vurgusu, trim/extend önizlemesi
    // ve çizim lastik bandı yine de görünür — akıcılık + canlı geri bildirim.
    if (draft.interactiveNavigation && !draft.snapOnly) {
        drawMotionFeedback();
        /* Eski satır içi geri bildirim bloğu lambda'ya taşındı — bu kalan
           kısım ölü kod; derleyiciyi sessiz tutmak için koşul false. */
        if (false) {
        const bool feedbackActive = draft.transformCommand != TransformCommand::None ||
                                    !draft.selectedModels.empty() ||
                                    (draft.drawingActive && draft.anchor && draft.cursor);
        if (feedbackActive) {
            if (!draft.selectedModels.empty()) {
                HPEN selectedPen = CreatePen(PS_SOLID, 3, RGB(90, 255, 145));
                SelectObject(dc, selectedPen);
                for (const auto index : draft.selectedModels) {
                    if (index < document.models().size() && document.modelIsEditable(index))
                        drawModel(document.models()[index]);
                }
                SelectObject(dc, stockPen);
                DeleteObject(selectedPen);
            }
            if (draft.transformCommand != TransformCommand::None &&
                draft.transformPhase == TransformPhase::Destination &&
                draft.transformBase && draft.cursor) {
                const POINT basePoint = projectPoint(*draft.transformBase);
                const POINT destinationPoint = projectPoint(*draft.cursor);
                const COLORREF trackerColor = [&]() -> COLORREF {
                    switch (draft.orthoAxis) {
                    case OrthoAxis::X: return RGB(235, 82, 96);   // Red
                    case OrthoAxis::Y: return RGB(72, 211, 121);  // Green
                    case OrthoAxis::Z: return RGB(78, 148, 255);  // Blue
                    default: return RGB(255, 206, 84);            // Yellow (no ortho)
                    }
                }();
                HPEN trackerPen = CreatePen(PS_DOT, 1, trackerColor);
                SelectObject(dc, trackerPen);
                line(dc, basePoint.x, basePoint.y, destinationPoint.x, destinationPoint.y);
                SelectObject(dc, stockPen);
                DeleteObject(trackerPen);

                HPEN transformPreview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
                SelectObject(dc, transformPreview);
                for (const auto index : draft.selectedModels) {
                    if (index >= document.models().size()) continue;
                    if (draft.transformCommand == TransformCommand::Mirror) {
                        const auto preview = mode == EditMode::View3D
                            ? mirrorModelOnPlane(document.models()[index], *draft.transformBase,
                                                 *draft.cursor, draft.workPlane)
                            : mirrorModel2D(document.models()[index], *draft.transformBase, *draft.cursor);
                        if (preview) drawModel(*preview);
                    } else if (draft.transformCommand == TransformCommand::LinearArray &&
                               draft.arrayItemCount) {
                        for (const auto& preview : linearArray2D(document.models()[index],
                                                                 *draft.arrayItemCount,
                                                                 *draft.cursor - *draft.transformBase))
                            drawModel(preview);
                    } else {
                        drawModel(document.models()[index], *draft.cursor - *draft.transformBase);
                    }
                }
                SelectObject(dc, stockPen);
                DeleteObject(transformPreview);
            }
            if ((draft.transformCommand == TransformCommand::Trim ||
                 draft.transformCommand == TransformCommand::Extend) &&
                draft.transformPhase == TransformPhase::Destination &&
                !draft.modifierBoundaries.empty() && !draft.trimExtendPreviewSuppressed) {
                Vec3 pick{};
                std::optional<std::size_t> target;
                if (mode == EditMode::Draw2D) {
                    pick = camera.unproject2D({static_cast<double>(draft.cursorScreen.x),
                                               static_cast<double>(draft.cursorScreen.y)},
                                              width, height);
                    target = hitTestModel2D(pick, document, 10.0 / (60.0 * camera.zoom()));
                } else {
                    WorkPlane targetPlane = draft.workPlane;
                    if (!draft.modifierBoundaries.front().vertices().empty())
                        targetPlane.origin = draft.modifierBoundaries.front().vertices().front();
                    if (const auto projectedPick = camera.unprojectToPlane(
                            {static_cast<double>(draft.cursorScreen.x),
                             static_cast<double>(draft.cursorScreen.y)}, width, height, targetPlane))
                        pick = *projectedPick;
                    target = hitTestModel3D({static_cast<double>(draft.cursorScreen.x),
                                             static_cast<double>(draft.cursorScreen.y)},
                                            document, camera, width, height, 10.0);
                }
                if (target && *target < document.models().size()) {
                    HPEN previewPen = CreatePen(PS_DASH, 2, RGB(255, 206, 84));
                    SelectObject(dc, previewPen);
                    if (draft.transformCommand == TransformCommand::Trim) {
                        const auto result = mode == EditMode::View3D
                            ? trimLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                              pick, draft.workPlane)
                            : trimLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                        if (result)
                            for (const auto& segment : *result) drawModel(segment);
                    } else {
                        const auto result = mode == EditMode::View3D
                            ? extendLineOnPlane(document.models()[*target], draft.modifierBoundaries,
                                                pick, draft.workPlane)
                            : extendLine2D(document.models()[*target], draft.modifierBoundaries, pick);
                        if (result) drawModel(*result);
                    }
                    SelectObject(dc, stockPen);
                    DeleteObject(previewPen);
                }
            }
            if (draft.drawingActive && draft.anchor && draft.cursor) {
                const POINT a = projectPoint(*draft.anchor);
                const POINT b = projectPoint(*draft.cursor);
                HPEN preview = CreatePen(PS_DASH, 1, RGB(255, 206, 84));
                SelectObject(dc, preview);
                line(dc, a.x, a.y, b.x, b.y);
                SelectObject(dc, stockPen);
                DeleteObject(preview);
            }
        }
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    if (!draft.snapOnly)
        BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);

    // GPU renders lines on top of GDI content
    if (useGpuLines) {
        FrameInfo frameInfo{width, height, false, 1.0, 0.0, 0.0};
        glBackend->beginFrame(frameInfo);
        if (!gpuBatch.empty()) glBackend->renderWireframeBatch(gpuBatch, camera);
        if (guiOverlay_) guiOverlay_();
        glBackend->endFrame();
        performance.drawCalls += glBackend->drawCallsPerFrame();
    }

    finishPerformanceSample(false);
}

const FramePerformanceSample& Renderer::performanceStats() const noexcept {
    return performanceTracker_.latest();
}

} // namespace mm
