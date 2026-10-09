#pragma once

// IKI NOKTALI GORUNUS (Tekla "Create view by two points").
// P1 -> P2 dogrultusu ekranda SOLDAN SAGA, dunya Z'si YUKARI; bakis yonu
// XY duzlemine PARALEL (gorunus duzlemi XY'ye DIK). Z farki gorunusu
// egmez — yalniz P1->P2'nin yatay (XY) izdusumu kullanilir.
// Tasinabilir cekirdek (windows.h yok) — Linux testinde dogrulanir.

#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace mm {

struct ViewDefinition {
    std::wstring name;
    Vec3 origin{};     // P1
    Vec3 right{1, 0, 0}; // ekran saga (yatay, birim)
    Vec3 up{0, 0, 1};    // ekran yukari (= dunya Z)
    Vec3 normal{0, -1, 0}; // izleyiciye dogru (right x up); bakis = -normal
    double length{};   // P1-P2 yatay mesafe
    // Derinlik dilimi (Tekla "view depth"): gorunus duzleminin onu/arkasi.
    double depthFront{1000.0}; // izleyici tarafi (+normal)
    double depthBack{1000.0};  // arka taraf (-normal)
};

inline constexpr double kDefaultViewDepth = 1000.0; // mm

inline std::optional<ViewDefinition> viewFromTwoPoints(const Vec3& p1, const Vec3& p2,
                                                       double depthFront = kDefaultViewDepth,
                                                       double depthBack = kDefaultViewDepth) {
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    const double horizontal = std::sqrt(dx * dx + dy * dy);
    // Ayni nokta veya dusey cift (XY'de cakisan): yon tanimsiz.
    if (!(horizontal > 1e-6)) return std::nullopt;
    const auto sane = [](double d) {
        return std::isfinite(d) && d >= 0.0 ? d : kDefaultViewDepth;
    };
    ViewDefinition v;
    v.origin = p1;
    v.right = {dx / horizontal, dy / horizontal, 0.0};
    v.up = {0.0, 0.0, 1.0};
    // normal = right x up = (ry, -rx, 0): izleyici tarafi.
    v.normal = {v.right.y, -v.right.x, 0.0};
    v.length = horizontal;
    v.depthFront = sane(depthFront);
    v.depthBack = sane(depthBack);
    return v;
}

// Sinir kutusu derinlik diliminde mi? (kutunun normal yonundeki izdusum
// araligi [-back, +front] ile kesisiyorsa gorunur.)
inline bool boundsInViewSlab(const Bounds3& b, const ViewDefinition& v) noexcept {
    const Vec3 c{(b.minimum.x + b.maximum.x) * 0.5, (b.minimum.y + b.maximum.y) * 0.5,
                 (b.minimum.z + b.maximum.z) * 0.5};
    const Vec3 h{(b.maximum.x - b.minimum.x) * 0.5, (b.maximum.y - b.minimum.y) * 0.5,
                 (b.maximum.z - b.minimum.z) * 0.5};
    const double center = (c.x - v.origin.x) * v.normal.x + (c.y - v.origin.y) * v.normal.y +
                          (c.z - v.origin.z) * v.normal.z;
    const double radius = std::abs(h.x * v.normal.x) + std::abs(h.y * v.normal.y) +
                          std::abs(h.z * v.normal.z);
    return center + radius >= -v.depthBack && center - radius <= v.depthFront;
}

// Gorunusun ilk "sigdir" kutusu: dilimdeki modellerin sinirlari + P1/P2.
// Dilim bossa P1-P2 hattinin etrafinda makul bir kutu (bos ekran degil).
inline Bounds3 viewFitBounds(const std::vector<Bounds3>& modelBounds, const ViewDefinition& v) {
    const Vec3 p2{v.origin.x + v.right.x * v.length, v.origin.y + v.right.y * v.length, v.origin.z};
    Bounds3 box{{std::min(v.origin.x, p2.x), std::min(v.origin.y, p2.y), v.origin.z},
                {std::max(v.origin.x, p2.x), std::max(v.origin.y, p2.y), v.origin.z}};
    bool any = false;
    for (const auto& b : modelBounds) {
        if (!std::isfinite(b.minimum.x) || !std::isfinite(b.maximum.x) ||
            !std::isfinite(b.minimum.z) || !std::isfinite(b.maximum.z)) continue;
        if (!boundsInViewSlab(b, v)) continue;
        any = true;
        box.minimum = {std::min(box.minimum.x, b.minimum.x), std::min(box.minimum.y, b.minimum.y),
                       std::min(box.minimum.z, b.minimum.z)};
        box.maximum = {std::max(box.maximum.x, b.maximum.x), std::max(box.maximum.y, b.maximum.y),
                       std::max(box.maximum.z, b.maximum.z)};
    }
    if (!any) { // bos dilim: hattin dortte biri kadar dusey pay
        const double pad = std::max(v.length * 0.25, 500.0);
        box.minimum.z -= pad;
        box.maximum.z += pad;
    }
    return box;
}

} // namespace mm
