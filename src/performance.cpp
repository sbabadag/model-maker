#include "model_maker/performance.hpp"

#include <algorithm>

namespace mm {

void FramePerformanceTracker::record(FramePerformanceSample sample,
                                     double secondsSincePreviousFrame) noexcept {
    sample.culledEntities = sample.totalEntities > sample.visibleEntities
        ? sample.totalEntities - sample.visibleEntities : 0;
    const double instantaneousFps = secondsSincePreviousFrame > 0.0
        ? 1.0 / secondsSincePreviousFrame : 0.0;
    sample.framesPerSecond = hasSample_
        ? latest_.framesPerSecond * 0.85 + instantaneousFps * 0.15
        : instantaneousFps;
    sample.frameNumber = latest_.frameNumber + 1;
    latest_ = sample;
    hasSample_ = true;
}

const FramePerformanceSample& FramePerformanceTracker::latest() const noexcept {
    return latest_;
}

bool FrameIndexStampSet::assign(std::size_t universeSize,
                                const std::vector<std::size_t>& indices) {
    const bool grew = stamps_.size() < universeSize;
    if (grew) stamps_.resize(universeSize, 0);
    universeSize_ = universeSize;
    ++generation_;
    if (generation_ == 0) {
        std::fill(stamps_.begin(), stamps_.end(), 0);
        generation_ = 1;
    }
    for (const std::size_t index : indices)
        if (index < universeSize_) stamps_[index] = generation_;
    return grew;
}

bool FrameIndexStampSet::contains(std::size_t index) const noexcept {
    return index < universeSize_ && stamps_[index] == generation_;
}

} // namespace mm
