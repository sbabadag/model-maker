#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm {

struct FramePerformanceSample {
    double cpuFrameMilliseconds{};
    double spatialQueryMilliseconds{};
    std::size_t totalEntities{};
    std::size_t visibleEntities{};
    std::size_t renderedEntities{};
    std::size_t culledEntities{};
    std::size_t drawCalls{};
    std::size_t projectedVertices{};
    std::size_t frameBufferGrowths{};
    double framesPerSecond{};
    std::uint64_t frameNumber{};
    bool rasterPreview{};
};

class FramePerformanceTracker {
public:
    void record(FramePerformanceSample sample, double secondsSincePreviousFrame) noexcept;
    const FramePerformanceSample& latest() const noexcept;

private:
    FramePerformanceSample latest_{};
    bool hasSample_{};
};

class FrameIndexStampSet {
public:
    bool assign(std::size_t universeSize, const std::vector<std::size_t>& indices);
    bool contains(std::size_t index) const noexcept;

private:
    std::vector<std::uint32_t> stamps_;
    std::uint32_t generation_{};
    std::size_t universeSize_{};
};

} // namespace mm
