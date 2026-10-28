#include "model_maker/axis_delta.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace mm {
namespace {

bool isSeparator(wchar_t c) noexcept { return c == L',' || c == L';' || c == L' ' || c == L'\t'; }

} // namespace

std::optional<Vec3> parseAxisDeltaText(const std::wstring& text,
                                       const std::optional<Vec3>& base,
                                       const std::optional<Vec3>& cursor) {
    std::vector<double> values;
    std::wstring current;
    const auto flush = [&]() -> bool {
        if (current.empty()) return true;
        std::size_t used = 0;
        try {
            const double v = std::stod(current, &used);
            if (used != current.size() || !std::isfinite(v)) return false;
            values.push_back(v);
        } catch (...) {
            return false;
        }
        current.clear();
        return true;
    };
    for (const wchar_t c : text) {
        if (isSeparator(c)) {
            if (!flush()) return std::nullopt;
            continue;
        }
        current.push_back(c);
    }
    if (!flush()) return std::nullopt;
    if (values.empty() || values.size() > 3) return std::nullopt;

    if (values.size() == 3) return Vec3{values[0], values[1], values[2]};
    if (values.size() == 2) return Vec3{values[0], values[1], 0.0};

    // Tek sayi: baz -> imlec yonunde; yon tanimsizsa +X.
    Vec3 dir{1.0, 0.0, 0.0};
    if (base && cursor) {
        const Vec3 d{cursor->x - base->x, cursor->y - base->y, cursor->z - base->z};
        const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 1e-9) dir = {d.x / len, d.y / len, d.z / len};
    }
    return Vec3{dir.x * values[0], dir.y * values[0], dir.z * values[0]};
}

} // namespace mm
