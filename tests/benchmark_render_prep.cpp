#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
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

    std::cout << std::fixed << std::setprecision(3)
              << "entities=" << entityCount
              << " visible=" << visible.size()
              << " baseline_vertex_scan_ms=" << baseline
              << " cached_bounds_ms=" << cached
              << " speedup=" << (cached > 0.0 ? baseline / cached : 0.0) << "x"
              << " snap3d_ms=" << snap3D << "\n";
    return visible.size() == entityCount && benchmarkSink != 0.0 ? 0 : 1;
}
