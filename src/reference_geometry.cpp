#include "model_maker/reference_geometry.hpp"

#include <cmath>

namespace mm {

ReferencePlane resolveBoundaryPlaneFacing(const Vec3& bmin, const Vec3& bmax,
                                          const Vec3& outwardDir) noexcept {
    // 6 bbox yuzu: dis normaller +-X +-Y +-Z. outwardDir ile EN PARALEL (en
    // buyuk pozitif dot) olan yuzu sec. outwardDir sifirsa +X varsayilir.
    const Vec3 dir = [&]() {
        const double l = std::sqrt(outwardDir.x * outwardDir.x + outwardDir.y * outwardDir.y +
                                   outwardDir.z * outwardDir.z);
        if (l < 1e-12) return Vec3{1.0, 0.0, 0.0};
        return Vec3{outwardDir.x / l, outwardDir.y / l, outwardDir.z / l};
    }();

    const double cx = 0.5 * (bmin.x + bmax.x);
    const double cy = 0.5 * (bmin.y + bmax.y);
    const double cz = 0.5 * (bmin.z + bmax.z);
    const Vec3 normals[6] = {
        {1.0, 0.0, 0.0},  {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
        {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0},  {0.0, 0.0, -1.0},
    };
    const Vec3 origins[6] = {
        {bmax.x, cy, cz}, {bmin.x, cy, cz}, {cx, bmax.y, cz},
        {cx, bmin.y, cz}, {cx, cy, bmax.z}, {cx, cy, bmin.z},
    };
    ReferencePlane out{origins[0], normals[0]};
    double bestDot = -2.0;
    for (int i = 0; i < 6; ++i) {
        const double d = normals[i].x * dir.x + normals[i].y * dir.y + normals[i].z * dir.z;
        if (d > bestDot) {
            bestDot = d;
            out = ReferencePlane{origins[i], normals[i]};
        }
    }
    return out;
}

} // namespace mm
