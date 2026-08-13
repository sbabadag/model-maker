#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/performance.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
volatile double benchmarkSink = 0.0;

template <typename Function>
double medianMilliseconds(Function&& function, int repetitions = 15) {
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
    constexpr std::size_t entityCount = 100'000;
    mm::Document document;
    document.reserveModels(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index) {
        const double x = static_cast<double>(index % 1000);
        const double y = static_cast<double>(index / 1000);
        document.addLine({x, y, 0.0}, {x + 0.75, y + 0.25, 0.0});
    }

    const auto visible = document.query2D({-1.0, -1.0, 0.0}, {1001.0, 101.0, 0.0});
    const auto baseline = medianMilliseconds([&] {
        double checksum = 0.0;
        for (const auto index : visible) {
            const auto& vertices = document.models()[index].vertices();
            if (vertices.empty()) continue;
            mm::Vec3 minimum = vertices.front();
            mm::Vec3 maximum = minimum;
            for (const auto& vertex : vertices) {
                minimum.x = std::min(minimum.x, vertex.x);
                minimum.y = std::min(minimum.y, vertex.y);
                minimum.z = std::min(minimum.z, vertex.z);
                maximum.x = std::max(maximum.x, vertex.x);
                maximum.y = std::max(maximum.y, vertex.y);
                maximum.z = std::max(maximum.z, vertex.z);
            }
            checksum += (minimum.x + maximum.x + minimum.y + maximum.y) * 0.5;
        }
        return checksum;
    });

    const auto cached = medianMilliseconds([&] {
        double checksum = 0.0;
        const auto& bounds = document.modelBounds();
        for (const auto index : visible) {
            const auto& value = bounds[index];
            checksum += (value.minimum.x + value.maximum.x +
                         value.minimum.y + value.maximum.y) * 0.5;
        }
        return checksum;
    });

    mm::Camera camera;
    camera.setView(mm::StandardView::Front);
    constexpr int viewportWidth = 1200;
    constexpr int viewportHeight = 800;
    const mm::Vec2 endpoint = camera.project({0.0, 0.0, 0.0}, viewportWidth, viewportHeight);
    mm::SnapTypeMask endpointOnly{};
    endpointOnly[static_cast<std::size_t>(mm::SnapType::Endpoint)] = true;
    const auto snap3D = medianMilliseconds([&] {
        const auto result = mm::SnapEngine::snap3D(endpoint, document, camera,
            viewportWidth, viewportHeight, 10.0, 1.0, 0.0,
            true, false, std::nullopt, &endpointOnly);
        return result.point.x + result.point.y +
               static_cast<double>(result.type == mm::SnapType::Endpoint);
    });

    std::vector<std::size_t> all3DModels;
    const auto full3DTraversal = medianMilliseconds([&] {
        all3DModels.resize(document.models().size());
        std::iota(all3DModels.begin(), all3DModels.end(), std::size_t{0});
        return static_cast<double>(all3DModels.size());
    });
    std::vector<std::size_t> culled3DModels;
    const auto projected3DCulling = medianMilliseconds([&] {
        culled3DModels = document.queryBounds([&](const mm::Bounds3& bounds) {
            return mm::projectedBoundsIntersectsViewport(
                bounds, camera, viewportWidth, viewportHeight);
        });
        return static_cast<double>(culled3DModels.size());
    });

    constexpr std::size_t selectedCount = 10'000;
    std::vector<std::size_t> selectedModels(selectedCount);
    for (std::size_t index = 0; index < selectedCount; ++index)
        selectedModels[index] = index * (entityCount / selectedCount);
    const auto linearSelectionMembership = medianMilliseconds([&] {
        std::size_t matches = 0;
        for (std::size_t index = 0; index < entityCount; ++index)
            if (std::find(selectedModels.begin(), selectedModels.end(), index) != selectedModels.end())
                ++matches;
        return static_cast<double>(matches);
    }, 5);
    mm::FrameIndexStampSet selectedIndexSet;
    selectedIndexSet.assign(entityCount, selectedModels);
    const auto stampedSelectionMembership = medianMilliseconds([&] {
        selectedIndexSet.assign(entityCount, selectedModels);
        std::size_t matches = 0;
        for (std::size_t index = 0; index < entityCount; ++index)
            if (selectedIndexSet.contains(index)) ++matches;
        return static_cast<double>(matches);
    }, 5);

    // Isolated projection cost: exercises Camera::viewTransform for every vertex.
    constexpr int projectionSamples = 1'000'000;
    const auto projection1M = medianMilliseconds([&] {
        double checksum = 0.0;
        for (int i = 0; i < projectionSamples; ++i) {
            const mm::Vec3 p{static_cast<double>(i % 1000),
                             static_cast<double>((i / 1000) % 1000),
                             static_cast<double>(i / 1'000'000)};
            const mm::Vec2 projected = camera.project(p, viewportWidth, viewportHeight);
            checksum += projected.x + projected.y;
        }
        return checksum;
    });

    std::cout << std::fixed << std::setprecision(3)
              << "entities=" << entityCount
              << " visible=" << visible.size()
              << " baseline_vertex_scan_ms=" << baseline
              << " cached_bounds_ms=" << cached
              << " speedup=" << (cached > 0.0 ? baseline / cached : 0.0) << "x"
              << " snap3d_ms=" << snap3D
              << " full_3d_list_ms=" << full3DTraversal
              << " projected_3d_query_ms=" << projected3DCulling
              << " visible_3d=" << culled3DModels.size()
              << " culled_3d=" << (entityCount - culled3DModels.size())
              << " linear_selection_ms=" << linearSelectionMembership
              << " stamped_selection_ms=" << stampedSelectionMembership
              << " selection_speedup=" << (stampedSelectionMembership > 0.0
                    ? linearSelectionMembership / stampedSelectionMembership : 0.0) << "x"
              << " projection_1m_ms=" << projection1M << "\n";
    return visible.size() == entityCount && !culled3DModels.empty() &&
           culled3DModels.size() < entityCount && benchmarkSink != 0.0 ? 0 : 1;
}
