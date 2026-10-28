#include "model_maker/linear_delta.hpp"

namespace mm {

std::vector<Vec3> linearCopyOffsets(const Vec3& delta, int count) {
    std::vector<Vec3> offsets;
    if (count < 1) return offsets;
    const int limited = count > kLinearCopyMaxCount ? kLinearCopyMaxCount : count;
    offsets.reserve(static_cast<std::size_t>(limited));
    for (int k = 1; k <= limited; ++k)
        offsets.push_back(Vec3{delta.x * static_cast<double>(k),
                               delta.y * static_cast<double>(k),
                               delta.z * static_cast<double>(k)});
    return offsets;
}

} // namespace mm
