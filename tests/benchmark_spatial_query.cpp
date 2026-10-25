#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {
volatile double benchmarkSink = 0.0;

template <typename Function>
double medianMilliseconds(Function&& function, int repetitions = 9) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        const auto start = std::chrono::steady_clock::now();
        benchmarkSink = function();
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}
}

int main() {
    constexpr std::size_t scales[] = {10'000, 50'000, 100'000, 250'000};
    constexpr int viewportWidth = 1200;
    constexpr int viewportHeight = 800;
    constexpr double pickTolerance = 10.0 / 60.0; // world units, matches app (zoom 1.0)

    for (const std::size_t entityCount : scales) {
        mm::Document document;
        document.reserveModels(entityCount);
        for (std::size_t index = 0; index < entityCount; ++index) {
            const double x = static_cast<double>(index % 1000);
            const double y = static_cast<double>(index / 1000);
            document.addLine({x, y, 0.0}, {x + 0.75, y + 0.25, 0.0});
        }

        mm::Camera camera; // Front view: projection/culling/picking tests
        camera.setView(mm::StandardView::Front);
        mm::Camera fitted; // isometric + fit to grid: realistic on-screen 3D tests
        fitted.setView(mm::StandardView::Isometric);
        const auto bounds = document.bounds();
        if (bounds) fitted.fit3D(bounds->minimum, bounds->maximum,
                                 viewportWidth, viewportHeight, 40.0);

        const double gridRows = static_cast<double>(entityCount / 1000);
        const mm::Vec3 cursor{500.0, gridRows * 0.5, 0.0};
        const mm::Vec2 screenCursor = camera.project(cursor, viewportWidth, viewportHeight);
        const mm::Vec2 fittedCenter = fitted.project(cursor, viewportWidth, viewportHeight);
        const mm::Vec3 windowMin{100.0, 2.0, 0.0};
        const mm::Vec3 windowMax{900.0, gridRows - 2.0, 0.0};

        // 3D window rects in screen space around the fitted viewport center:
        // small = ~1.4% of viewport area (prefilter path), mid = 16% (direct scan path).
        const double smallW = 0.12 * viewportWidth, smallH = 0.12 * viewportHeight;
        const double largeW = 0.40 * viewportWidth, largeH = 0.40 * viewportHeight;
        const mm::Vec2 winSmallA{fittedCenter.x - smallW * 0.5, fittedCenter.y - smallH * 0.5};
        const mm::Vec2 winSmallB{fittedCenter.x + smallW * 0.5, fittedCenter.y + smallH * 0.5};
        const mm::Vec2 winLargeA{fittedCenter.x - largeW * 0.5, fittedCenter.y - largeH * 0.5};
        const mm::Vec2 winLargeB{fittedCenter.x + largeW * 0.5, fittedCenter.y + largeH * 0.5};

        // Warm up spatial index + effective cache.
        (void)document.query2D({cursor.x - 1.0, cursor.y - 1.0, -1e100},
                               {cursor.x + 1.0, cursor.y + 1.0, 1e100});
        (void)document.effectiveProperties(0u);

        const int reps = entityCount > 100'000 ? 5 : 9;

        const double query2dPoint = medianMilliseconds([&] {
            const auto r = document.query2D({cursor.x - pickTolerance, cursor.y - pickTolerance, -1e100},
                                            {cursor.x + pickTolerance, cursor.y + pickTolerance, 1e100});
            return static_cast<double>(r.size());
        }, reps);
        const double query2dWindow = medianMilliseconds([&] {
            const auto r = document.query2D(windowMin, windowMax);
            return static_cast<double>(r.size());
        }, reps);
        const double cull3d = medianMilliseconds([&] {
            const auto r = document.queryBounds([&](const mm::Bounds3& b) {
                return mm::projectedBoundsIntersectsViewport(b, camera,
                                                             viewportWidth, viewportHeight);
            });
            return static_cast<double>(r.size());
        }, reps);
        double pick2dResult = -1.0;
        const double pick2d = medianMilliseconds([&] {
            const auto r = mm::hitTestModel2D(cursor, document, pickTolerance);
            pick2dResult = r ? static_cast<double>(*r) : -1.0;
            return pick2dResult;
        }, reps);
        double pick3dResult = -1.0;
        const double pick3d = medianMilliseconds([&] {
            const auto r = mm::hitTestModel3D(fittedCenter, document, fitted,
                                              viewportWidth, viewportHeight, 10.0);
            pick3dResult = r ? static_cast<double>(*r) : -1.0;
            return pick3dResult;
        }, reps);
        // Reference: pre-optimization full-scan pick (same logic as old hitTestModel3D).
        double pick3dRefResult = -1.0;
        const double pick3dRef = medianMilliseconds([&] {
            double best = 10.0;
            std::size_t bestIdx = static_cast<std::size_t>(-1);
            for (std::size_t i = 0; i < entityCount; ++i) {
                if (!document.modelIsEditable(i)) continue;
                const auto& m = document.models()[i];
                for (const auto& e : m.edges()) {
                    const mm::Vec2 a = fitted.project(m.vertices()[e.from], viewportWidth, viewportHeight);
                    const mm::Vec2 b = fitted.project(m.vertices()[e.to], viewportWidth, viewportHeight);
                    const double dx = b.x - a.x, dy = b.y - a.y;
                    const double len2 = dx * dx + dy * dy;
                    double d;
                    if (len2 <= 1e-9) d = std::hypot(fittedCenter.x - a.x, fittedCenter.y - a.y);
                    else {
                        const double t = std::clamp(((fittedCenter.x - a.x) * dx +
                                                     (fittedCenter.y - a.y) * dy) / len2, 0.0, 1.0);
                        d = std::hypot(fittedCenter.x - (a.x + t * dx), fittedCenter.y - (a.y + t * dy));
                    }
                    if (d <= best) { best = d; bestIdx = i; }
                }
                if (m.edges().empty() && !m.vertices().empty()) {
                    const mm::Vec2 a = fitted.project(m.vertices().front(), viewportWidth, viewportHeight);
                    const double d = std::hypot(fittedCenter.x - a.x, fittedCenter.y - a.y);
                    if (d <= best) { best = d; bestIdx = i; }
                }
            }
            pick3dRefResult = bestIdx == static_cast<std::size_t>(-1)
                ? -1.0 : static_cast<double>(bestIdx);
            return pick3dRefResult;
        }, reps);
        double window2dCount = -1.0;
        const double window2d = medianMilliseconds([&] {
            const auto r = mm::selectModelsInRect2D(windowMin, windowMax, document, false);
            window2dCount = static_cast<double>(r.size());
            return window2dCount;
        }, reps);
        double win3dSmallCount = -1.0;
        const double win3dSmall = medianMilliseconds([&] {
            const auto r = mm::selectModelsInRect3D(winSmallA, winSmallB, document, fitted,
                                                    viewportWidth, viewportHeight, false);
            win3dSmallCount = static_cast<double>(r.size());
            return win3dSmallCount;
        }, reps);
        double win3dLargeCount = -1.0;
        const double win3dLarge = medianMilliseconds([&] {
            const auto r = mm::selectModelsInRect3D(winLargeA, winLargeB, document, fitted,
                                                    viewportWidth, viewportHeight, false);
            win3dLargeCount = static_cast<double>(r.size());
            return win3dLargeCount;
        }, reps);
        double cross2dCount = -1.0;
        const double cross2d = medianMilliseconds([&] {
            const auto r = mm::selectModelsInRect2D(windowMin, windowMax, document, true);
            cross2dCount = static_cast<double>(r.size());
            return cross2dCount;
        }, reps);
        const double snap2d = medianMilliseconds([&] {
            const auto r = mm::SnapEngine::snap(cursor, document, pickTolerance, 1.0,
                                                true, false, std::nullopt, nullptr);
            return r.point.x + r.point.y + static_cast<double>(r.type != mm::SnapType::None);
        }, reps);
        const double snap3d = medianMilliseconds([&] {
            const auto r = mm::SnapEngine::snap3D(fittedCenter, document, fitted,
                                                  viewportWidth, viewportHeight, 10.0, 1.0, 0.0,
                                                  true, false, std::nullopt, nullptr);
            return r.point.x + r.point.y + static_cast<double>(r.type != mm::SnapType::None);
        }, reps);

        std::printf("scale=%zu q2d_pt=%.3f q2d_win=%.3f cull3d=%.3f pick2d=%.3f pick3d=%.3f pick3d_ref=%.3f "
                    "win2d=%.3f win3d_small=%.3f win3d_mid=%.3f cross2d=%.3f snap2d=%.3f snap3d=%.3f "
                    "| picked2d=%.0f picked3d=%.0f ref=%.0f win2d_n=%.0f win3d_small_n=%.0f win3d_mid_n=%.0f cross2d_n=%.0f\n",
                    entityCount, query2dPoint, query2dWindow, cull3d, pick2d, pick3d, pick3dRef,
                    window2d, win3dSmall, win3dLarge, cross2d, snap2d, snap3d,
                    pick2dResult, pick3dResult, pick3dRefResult, window2dCount, win3dSmallCount, win3dLargeCount, cross2dCount);
    }
    return 0;
}
