#include "model_maker/cut_modifier.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

namespace {

inline double dot3(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross3(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double length3(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline std::optional<Vec3> normalized(const Vec3& v) noexcept {
    const double len = length3(v);
    if (!(len > 1e-12)) return std::nullopt;
    return Vec3{v.x / len, v.y / len, v.z / len};
}

} // namespace

double signedDistanceToPlane(const Vec3& origin, const Vec3& normal, const Vec3& p) noexcept {
    const auto n = normalized(normal);
    if (!n) return 0.0;
    return dot3(Vec3{p.x - origin.x, p.y - origin.y, p.z - origin.z}, *n);
}

int planeSide(const Vec3& origin, const Vec3& normal, const Vec3& p, double tol) noexcept {
    const double d = signedDistanceToPlane(origin, normal, p);
    if (d > tol) return 1;
    if (d < -tol) return -1;
    return 0;
}

Vec3 projectToPlane(const Vec3& origin, const Vec3& normal, const Vec3& p) noexcept {
    const auto n = normalized(normal);
    if (!n) return p;
    const double d = dot3(Vec3{p.x - origin.x, p.y - origin.y, p.z - origin.z}, *n);
    return {p.x - n->x * d, p.y - n->y * d, p.z - n->z * d};
}

std::optional<Vec3> polygonNormal(const std::vector<Vec3>& points) noexcept {
    if (points.size() < 3) return std::nullopt;
    Vec3 n{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec3& a = points[i];
        const Vec3& b = points[(i + 1) % points.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const auto un = normalized(n);
    if (!un) return std::nullopt;
    return un;
}

bool pointInPolygon(const std::vector<Vec3>& polygon, const std::optional<Vec3>& normal,
                    const Vec3& p) noexcept {
    if (polygon.size() < 3) return false;
    const auto n = normal ? normal : polygonNormal(polygon);
    if (!n) return false;
    // Duzlemde 2B taban kur: u = ilk kenar yonu, v = n x u.
    const auto u = normalized(Vec3{polygon[1].x - polygon[0].x, polygon[1].y - polygon[0].y,
                                   polygon[1].z - polygon[0].z});
    if (!u) return false;
    const Vec3 v = cross3(*n, *u);
    const auto to2D = [&](const Vec3& q) {
        const Vec3 d{q.x - polygon[0].x, q.y - polygon[0].y, q.z - polygon[0].z};
        return std::pair{dot3(d, *u), dot3(d, v)};
    };
    // Ray-cast (x-yonunde).
    const auto [px, py] = to2D(p);
    bool inside = false;
    const std::size_t count = polygon.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const auto [ix, iy] = to2D(polygon[i]);
        const auto [jx, jy] = to2D(polygon[j]);
        const bool crosses = (iy > py) != (jy > py) &&
                             (px < (jx - ix) * (py - iy) / (jy - iy) + ix);
        if (crosses) inside = !inside;
    }
    return inside;
}

std::optional<Vec3> segmentPlaneIntersection(const Vec3& a, const Vec3& b,
                                             const Vec3& origin, const Vec3& normal) noexcept {
    const auto n = normalized(normal);
    if (!n) return std::nullopt;
    const Vec3 dir{b.x - a.x, b.y - a.y, b.z - a.z};
    const double denom = dot3(dir, *n);
    if (std::abs(denom) < 1e-12) return std::nullopt; // paralel
    const double t = dot3(Vec3{origin.x - a.x, origin.y - a.y, origin.z - a.z}, *n) / denom;
    if (t < 0.0 || t > 1.0) return std::nullopt; // kesisim parca disinda
    return Vec3{a.x + dir.x * t, a.y + dir.y * t, a.z + dir.z * t};
}

std::optional<double> fittingTrimDistance(const Vec3& from, const Vec3& to,
                                          const Vec3& origin, const Vec3& normal) noexcept {
    const auto hit = segmentPlaneIntersection(from, to, origin, normal);
    if (!hit) return std::nullopt;
    return length3(Vec3{hit->x - from.x, hit->y - from.y, hit->z - from.z});
}

std::optional<bool> fittingKeepPositive(const Vec3& axisFrom, const Vec3& axisTo,
                                        const Vec3& origin, const Vec3& normal) noexcept {
    const double sFrom = signedDistanceToPlane(origin, normal, axisFrom);
    const double sTo = signedDistanceToPlane(origin, normal, axisTo);
    constexpr double tol = 1e-9;
    // Iki uc da duzleme yatik (eksen duzlemin icinde) -> kesim anlamsiz.
    if (std::abs(sFrom) < tol && std::abs(sTo) < tol) return std::nullopt;
    // Iki uc ayni tarafta -> duzlem ekseni KESMIYOR (uzatma/kisaltma durumu);
    // prototip yalniz kesen duzlemi destekler.
    if (sFrom * sTo > 0.0) return std::nullopt;
    // Kesişen duzlem: kisa tarafi at, uzun tarafi koru. Uzun taraf = isaretli
    // uzakligi (mutlak) BUYUK olan ucun bulundugu yon. keepPositive = o yon
    // +normal tarafindaysa true. Esitlikte (tam orta) to-tarafi korunur.
    if (std::abs(sTo) >= std::abs(sFrom)) return sTo > 0.0;
    return sFrom > 0.0;
}

std::optional<Vec3> lineCutPlaneNormal(const Vec3& from, const Vec3& to,
                                       const Vec3& referenceNormal) noexcept {
    const auto dir = normalized(Vec3{to.x - from.x, to.y - from.y, to.z - from.z});
    if (!dir) return std::nullopt;
    const auto ref = normalized(referenceNormal);
    if (!ref) return std::nullopt;
    // Kesme duzlemi cizgiyi ICERIR ve cizginin cizildigi duzleme DIKTIR:
    // normal = cizgiYonu x referenceNormal (ikisi de cizgi duzleminde oldugu
    // icin carpim hem cizgiye hem referans normale dik = istenen duzlem).
    return normalized(cross3(*dir, *ref));
}

std::vector<int> classifyVerticesAgainstPlane(const std::vector<Vec3>& vertices,
                                              const Vec3& origin, const Vec3& normal,
                                              double tol) noexcept {
    std::vector<int> result;
    result.reserve(vertices.size());
    for (const auto& v : vertices) result.push_back(planeSide(origin, normal, v, tol));
    return result;
}

} // namespace mm
