#include "model_maker/component_rebind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm {
namespace {

double dotOf(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 subOf(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 unitOf(const Vec3& v) noexcept {
    const double len = std::sqrt(dotOf(v, v));
    if (len <= 1e-12) return {0.0, 0.0, 0.0};
    return {v.x / len, v.y / len, v.z / len};
}

} // namespace

double distancePointToAxis(const Vec3& p, const Vec3& a, const Vec3& b) noexcept {
    const Vec3 ab = subOf(b, a);
    const double len2 = dotOf(ab, ab);
    if (len2 <= 1e-18) return std::sqrt(dotOf(subOf(p, a), subOf(p, a)));
    double t = dotOf(subOf(p, a), ab) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const Vec3 q{a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t};
    const Vec3 d = subOf(p, q);
    return std::sqrt(dotOf(d, d));
}

RebindResult findRebindParts(const std::vector<RebindCandidate>& candidates,
                             const Vec3& anchor,
                             const Vec3& mainDirHint,
                             const std::vector<Vec3>& secondaryDirHints,
                             std::size_t requiredSecondary,
                             double toleranceMm) {
    RebindResult out;
    struct Item {
        std::size_t index{0};
        Vec3 dir{};
        double distance{0.0};
    };
    std::vector<Item> items;
    items.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const RebindCandidate& c = candidates[i];
        const double d = distancePointToAxis(anchor, c.axisFrom, c.axisTo);
        // Kesit yaricapi kadar ek pay: baglanti levhasi uye ekseninden kesit
        // olcusu kadar uzakta olabilir (alin levhasi, fin levha).
        const double radius = 0.5 * std::sqrt(c.secWidth * c.secWidth + c.secHeight * c.secHeight);
        if (d > toleranceMm + radius) continue;
        items.push_back({i, unitOf(subOf(c.axisTo, c.axisFrom)), d});
    }
    if (items.empty()) return out;

    const auto byDistance = [](const Item& a, const Item& b) {
        if (std::fabs(a.distance - b.distance) > 1e-9) return a.distance < b.distance;
        return a.index < b.index;   // deterministik
    };
    std::sort(items.begin(), items.end(), byDistance);

    const Vec3 mainHint = unitOf(mainDirHint);
    const auto parallel = [](const Vec3& dir, const Vec3& hint) {
        return std::fabs(dotOf(dir, hint)) >= 0.7;   // ~45° tolerans
    };
    const auto perpendicular = [](const Vec3& dir, const Vec3& ref) {
        return std::fabs(dotOf(dir, ref)) <= 0.3;
    };

    // 1) MAIN: yon ipucu varsa ona paralel en yakin; yoksa en yakin.
    std::size_t mainPos = items.size();
    for (std::size_t k = 0; k < items.size(); ++k) {
        if (items[k].dir.x == 0.0 && items[k].dir.y == 0.0 && items[k].dir.z == 0.0) continue;
        if (mainHint.x != 0.0 || mainHint.y != 0.0 || mainHint.z != 0.0) {
            if (!parallel(items[k].dir, mainHint)) continue;
        }
        mainPos = k;
        break;
    }
    if (mainPos == items.size()) return out;   // dejenere eksenler / uygun aday yok
    out.main = candidates[items[mainPos].index].id;
    out.mainDistance = items[mainPos].distance;
    const Vec3 mainDir = items[mainPos].dir;
    std::vector<bool> used(items.size(), false);
    used[mainPos] = true;

    // 2) SECONDARY: main'e DIK adaylar; ipucu varsa ipucuna paralel olan tercih.
    for (std::size_t need = 0; need < requiredSecondary; ++need) {
        const Vec3 hint = need < secondaryDirHints.size() ? unitOf(secondaryDirHints[need]) : Vec3{};
        std::size_t bestPos = items.size();
        for (std::size_t k = 0; k < items.size(); ++k) {
            if (used[k]) continue;
            if (!perpendicular(items[k].dir, mainDir)) continue;
            if (hint.x != 0.0 || hint.y != 0.0 || hint.z != 0.0) {
                if (!parallel(items[k].dir, hint)) continue;
            }
            bestPos = k;   // items zaten uzakliga gore sirali
            break;
        }
        if (bestPos == items.size()) break;
        used[bestPos] = true;
        out.secondary.push_back(candidates[items[bestPos].index].id);
        out.secondaryDistances.push_back(items[bestPos].distance);
    }
    out.ok = out.main != kInvalidEntityId;
    return out;
}

} // namespace mm
