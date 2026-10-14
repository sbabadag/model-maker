#include "model_maker/ucs.hpp"

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

// Sag-el ortonormal cerceve kur: x ve y verilir (birbirine dik olmayabilir),
// x oncelikli normalize edilir, y x'ten arindirilip normalize edilir, z = x x y.
inline std::optional<Ucs> orthoFrame(const Vec3& origin, const Vec3& x, const Vec3& y) noexcept {
    const auto xa = normalized(x);
    if (!xa) return std::nullopt;
    const double d = dot3(*xa, y);
    const Vec3 yOrtho{y.x - d * xa->x, y.y - d * xa->y, y.z - d * xa->z};
    const auto ya = normalized(yOrtho);
    if (!ya) return std::nullopt;
    const Vec3 z = cross3(*xa, *ya);
    return Ucs{origin, *xa, *ya, z};
}

} // namespace

Ucs Ucs::world() noexcept { return Ucs{}; }

Ucs Ucs::planeXY(Vec3 origin) noexcept {
    return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
}

Ucs Ucs::planeXZ(Vec3 origin) noexcept {
    return Ucs{origin, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
}

Ucs Ucs::planeYZ(Vec3 origin) noexcept {
    return Ucs{origin, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
}

Ucs Ucs::fromOrigin(Vec3 origin) noexcept {
    Ucs u;
    u.origin = origin;
    return u;
}

std::optional<Ucs> Ucs::fromOriginAndX(Vec3 origin, Vec3 xPoint) noexcept {
    const auto xa = normalized(xPoint - origin);
    if (!xa) return std::nullopt;
    // X yonu dunya Z'ye paralelse (|x.z| ~ 1) dunya Z ile capraz carpim dejenere.
    if (std::abs(xa->z) > 0.999) return std::nullopt;
    // Y = Z x X (dunya Z sag-el; XY duzlemi yatay).
    const auto ya = normalized(cross3(Vec3{0, 0, 1}, *xa));
    if (!ya) return std::nullopt;
    const Vec3 za = cross3(*xa, *ya);
    return Ucs{origin, *xa, *ya, za};
}

std::optional<Ucs> Ucs::fromThreePoints(Vec3 origin, Vec3 xPoint, Vec3 yPoint) noexcept {
    const auto xa = normalized(xPoint - origin);
    if (!xa) return std::nullopt;
    const Vec3 xy = yPoint - origin;
    const auto za = normalized(cross3(*xa, xy));
    if (!za) return std::nullopt;
    const auto ya = normalized(cross3(*za, *xa));
    if (!ya) return std::nullopt;
    return Ucs{origin, *xa, *ya, *za};
}

Ucs Ucs::fromPlaneAndNormal(Vec3 origin, Vec3 normal) noexcept {
    const auto za = normalized(normal);
    if (!za) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    // Kararli referans: normal dunya Z'ye yakinsa ref = Y, degilse ref = Z.
    Vec3 ref{0, 0, 1};
    if (std::abs(za->z) > 0.99) ref = {0, 1, 0};
    const auto xa = normalized(cross3(ref, *za));
    if (!xa) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const auto ya = normalized(cross3(*za, *xa));
    if (!ya) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    return Ucs{origin, *xa, *ya, *za};
}

Ucs Ucs::fromPartFrame(Vec3 origin, Vec3 axis, Vec3 referenceNormal) noexcept {
    const auto za = normalized(axis);
    if (!za) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    // profileSectionFrame kurali: width = normalize(n x axis), height = axis x
    // width. n = referenceNormal; n ∥ axis ise dunya Z'ye dus.
    const auto n = normalized(referenceNormal);
    Vec3 nz{0, 0, 1};
    if (n) {
        nz = *n;
        if (std::abs(dot3(nz, *za)) > 0.999) nz = {0, 0, 1};
        if (std::abs(dot3(nz, *za)) > 0.999) nz = {0, 1, 0};
    }
    const auto xa = normalized(cross3(nz, *za));
    if (!xa) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const auto ya = normalized(cross3(*za, *xa));
    if (!ya) return Ucs{origin, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    return Ucs{origin, *xa, *ya, *za};
}

WorkPlane Ucs::toWorkPlane() const noexcept {
    return WorkPlane{origin, xAxis, yAxis, zAxis};
}

Ucs Ucs::fromWorkPlane(const WorkPlane& plane) noexcept {
    return Ucs{plane.origin, plane.u, plane.v, plane.normal};
}

Vec3 Ucs::toLocal(Vec3 worldPoint) const noexcept {
    const Vec3 d{worldPoint.x - origin.x, worldPoint.y - origin.y, worldPoint.z - origin.z};
    return {dot3(d, xAxis), dot3(d, yAxis), dot3(d, zAxis)};
}

Vec3 Ucs::toWorld(Vec3 localPoint) const noexcept {
    return {origin.x + xAxis.x * localPoint.x + yAxis.x * localPoint.y + zAxis.x * localPoint.z,
            origin.y + xAxis.y * localPoint.x + yAxis.y * localPoint.y + zAxis.y * localPoint.z,
            origin.z + xAxis.z * localPoint.x + yAxis.z * localPoint.y + zAxis.z * localPoint.z};
}

bool Ucs::isValid(double tolerance) const noexcept {
    const auto unit = [&](const Vec3& v) {
        return std::abs(length3(v) - 1.0) < tolerance;
    };
    const auto ortho = [&](const Vec3& a, const Vec3& b) {
        return std::abs(dot3(a, b)) < tolerance;
    };
    return unit(xAxis) && unit(yAxis) && unit(zAxis) && ortho(xAxis, yAxis) &&
           ortho(yAxis, zAxis) && ortho(xAxis, zAxis);
}

UcsView viewFromUcs(const Ucs& ucs) noexcept {
    return UcsView{ucs.xAxis, ucs.yAxis, ucs.zAxis};
}

UcsView viewPerpendicularToFace(Vec3 origin, Vec3 faceNormal) noexcept {
    const auto n = normalized(faceNormal);
    if (!n) return UcsView{};
    Vec3 up{0, 0, 1};
    if (std::abs(dot3(up, *n)) > 0.99) up = {0, 1, 0};
    const auto right = normalized(cross3(up, *n));
    if (!right) return UcsView{};
    const auto upOrtho = normalized(cross3(*n, *right));
    if (!upOrtho) return UcsView{};
    (void)origin;
    return UcsView{*right, *upOrtho, *n};
}

std::optional<UcsView> viewFromTwoPointsUcs(const Vec3& p1, const Vec3& p2) noexcept {
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    const double horizontal = std::sqrt(dx * dx + dy * dy);
    if (!(horizontal > 1e-6)) return std::nullopt;
    const Vec3 right{dx / horizontal, dy / horizontal, 0.0};
    const Vec3 up{0.0, 0.0, 1.0};
    const Vec3 normal{right.y, -right.x, 0.0}; // right x up
    return UcsView{right, up, normal};
}

std::optional<UcsView> viewFromThreePointsUcs(Vec3 origin, Vec3 xPoint, Vec3 yPoint) noexcept {
    const auto ucs = Ucs::fromThreePoints(origin, xPoint, yPoint);
    if (!ucs) return std::nullopt;
    return viewFromUcs(*ucs);
}

bool NamedViewList::add(NamedView view) noexcept {
    if (view.name.empty()) return false;
    for (auto& existing : views_) {
        if (existing.name == view.name) {
            existing = view;
            return true;
        }
    }
    views_.push_back(std::move(view));
    return true;
}

bool NamedViewList::remove(const std::wstring& name) noexcept {
    const auto it = std::find_if(views_.begin(), views_.end(),
                                 [&](const NamedView& v) { return v.name == name; });
    if (it == views_.end()) return false;
    views_.erase(it);
    return true;
}

void NamedViewList::clear() noexcept { views_.clear(); }

const NamedView* NamedViewList::find(const std::wstring& name) const noexcept {
    const auto it = std::find_if(views_.begin(), views_.end(),
                                 [&](const NamedView& v) { return v.name == name; });
    return it == views_.end() ? nullptr : &*it;
}

bool NamedViewList::contains(const std::wstring& name) const noexcept {
    return find(name) != nullptr;
}

std::size_t NamedViewList::size() const noexcept { return views_.size(); }

bool NamedViewList::empty() const noexcept { return views_.empty(); }

std::vector<std::wstring> NamedViewList::names() const {
    std::vector<std::wstring> result;
    result.reserve(views_.size());
    for (const auto& v : views_) result.push_back(v.name);
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace mm
