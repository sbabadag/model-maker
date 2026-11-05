#pragma once

// MM-8a/1 — DETERMINISTIK ETIKET YERLESIMI (Qt'siz, saf C++).
//
// Mock view'de olcu/etiket yazilari ust uste biniyordu. Bu katman etiketleri
// CAKISMASIZ ve DETERMINISTIK yerlestirir: aday konumlar + oncelik sirasi;
// hicbiri diger etiket kutulariyla ya da olcu cizgileriyle (segment) kesisemez;
// kaydirilan etiket icin kilavuz cizgisi (leader) isaretlenir.
//
// KRITIK: olcu hesabinda QFontMetrics KULLANILMAZ. Metin genisligi/yuksekligi
// sabit sabitlerle (charW, lineH) tahmin edilir; boylece AYNI girdi Linux'ta ve
// mingw'de AYNI yerlesimi verir (QFontMetrics platforma gore degisir).
//
// Koordinatlar EKRAN pikselidir (QPainter cizim uzayi). dpi = devicePixelRatio.

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace mm {

struct LabelBox {
    double x{0.0};   // sol-ust
    double y{0.0};
    double w{0.0};
    double h{0.0};
};

struct LabelRequest {
    std::string text;          // genislik tahmini icin
    double anchorX{0.0};       // cipa noktasi (ekran px)
    double anchorY{0.0};
    int priority{0};           // kucuk = once yerlestirilir
};

struct LabelObstacle {
    double x1{0.0}, y1{0.0}, x2{0.0}, y2{0.0};  // olcu cizgisi (segment)
};

struct LabelPlacement {
    LabelBox box{};            // yerlestirilen kutu (sol-ust + boyut)
    double anchorX{0.0}, anchorY{0.0};
    bool shifted{false};       // true ise cipa -> kutu kilavuz cizgisi cizilir
};

// Sabit metrikler (DPI ile olceklenir; platformdan bagimsiz).
inline double labelCharWidth(double dpi) noexcept { return 5.4 * dpi; }
inline double labelLineHeight(double dpi) noexcept { return 11.0 * dpi; }
inline double labelPad(double dpi) noexcept { return 2.0 * dpi; }
inline double labelGap(double dpi) noexcept { return 4.0 * dpi; }
inline double labelStep(double dpi) noexcept { return 9.0 * dpi; }

inline double labelTextWidth(const std::string& text, double dpi) noexcept {
    return static_cast<double>(text.size()) * labelCharWidth(dpi);
}

// Iki kutunun (m marjiyla genisletilmis) kesisip kesisnedigi.
inline bool labelBoxesOverlap(const LabelBox& a, const LabelBox& b,
                              double m) noexcept {
    return a.x < b.x + b.w + m && b.x < a.x + a.w + m &&
           a.y < b.y + b.h + m && b.y < a.y + a.h + m;
}

// Kutu bir segmentle kesisiyor mu? (Liang-Barsky kirpma; kutu ici kesim var mi.)
inline bool labelBoxHitsSegment(const LabelBox& b, const LabelObstacle& s,
                                double m) noexcept {
    const double x0 = b.x - m, y0 = b.y - m;
    const double x1 = b.x + b.w + m, y1 = b.y + b.h + m;
    const double dx = s.x2 - s.x1, dy = s.y2 - s.y1;
    double t0 = 0.0, t1 = 1.0;
    const auto clip = [&](double p, double q) -> bool {
        if (p == 0.0) return q >= 0.0;  // paralel: icerideyse devam
        const double r = q / p;
        if (p < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
        else { if (r < t0) return false; if (r < t1) t1 = r; }
        return true;
    };
    if (!clip(-dx, s.x1 - x0)) return false;
    if (!clip(dx, x1 - s.x1)) return false;
    if (!clip(-dy, s.y1 - y0)) return false;
    if (!clip(dy, y1 - s.y1)) return false;
    return t0 <= t1;
}

// Etiketleri deterministik bicimde, cakismasiz yerlestir.
//   reqs      : etiket istekleri (metin + cipa + oncelik)
//   obstacles : olcu cizgileri (etiket bunlarla kesisemez)
//   w,h       : gorunum (widget) boyutu
//   dpi       : devicePixelRatio
// Donus sirasi GIRIS SIRASIYLA aynidir (deterministik cagri icin).
inline std::vector<LabelPlacement> layoutLabels(
    const std::vector<LabelRequest>& reqs,
    const std::vector<LabelObstacle>& obstacles,
    double w, double h, double dpi) {
    std::vector<LabelPlacement> out(reqs.size());
    if (reqs.empty()) return out;

    const double cw = labelCharWidth(dpi);
    const double lh = labelLineHeight(dpi);
    const double pad = labelPad(dpi);
    const double gap = labelGap(dpi);
    const double step = labelStep(dpi);

    // Oncelik sirasi: kucuk priority once, esitlikte (anchorX, anchorY) artan.
    std::vector<std::size_t> order(reqs.size());
    for (std::size_t i = 0; i < reqs.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (reqs[a].priority != reqs[b].priority) return reqs[a].priority < reqs[b].priority;
        if (reqs[a].anchorX != reqs[b].anchorX) return reqs[a].anchorX < reqs[b].anchorX;
        return reqs[a].anchorY < reqs[b].anchorY;
    });

    std::vector<LabelBox> placed;
    placed.reserve(reqs.size());

    // Aday yonler (bx, by): etiketin cipaya gore ilk konumu.
    //   0: sag, 1: sol, 2: ust, 3: alt, 4..7: caprazlar.
    static const double dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, -1}, {0, 1}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}};

    for (std::size_t oi : order) {
        const LabelRequest& r = reqs[oi];
        const double lw = labelTextWidth(r.text, dpi) + 2.0 * pad;
        const double lhgt = lh;
        LabelBox chosen{};
        bool found = false;
        bool shifted = false;
        double leaderAnchorX = r.anchorX, leaderAnchorY = r.anchorY;

        for (int ring = 0; ring < 40 && !found; ++ring) {
            const double dist = gap + static_cast<double>(ring) * step;
            for (int di = 0; di < 8 && !found; ++di) {
                double bx, by;
                if (dirs[di][0] > 0) bx = r.anchorX + dist;
                else if (dirs[di][0] < 0) bx = r.anchorX - dist - lw;
                else bx = r.anchorX - lw * 0.5;
                if (dirs[di][1] > 0) by = r.anchorY + dist;
                else if (dirs[di][1] < 0) by = r.anchorY - dist - lhgt;
                else by = r.anchorY - lhgt * 0.5;
                // Gorunum icine kelepce (deterministik).
                bx = std::max(0.0, std::min(bx, w - lw));
                by = std::max(0.0, std::min(by, h - lhgt));
                LabelBox cand{bx, by, lw, lhgt};
                bool clash = false;
                for (const auto& pb : placed) {
                    if (labelBoxesOverlap(cand, pb, pad)) { clash = true; break; }
                }
                if (!clash) {
                    for (const auto& s : obstacles) {
                        if (labelBoxHitsSegment(cand, s, pad)) { clash = true; break; }
                    }
                }
                if (!clash) {
                    chosen = cand;
                    found = true;
                    // Cipa kutunun icinde degilse kilavuz cizgisi.
                    const bool insideX = r.anchorX >= cand.x && r.anchorX <= cand.x + cand.w;
                    const bool insideY = r.anchorY >= cand.y && r.anchorY <= cand.y + cand.h;
                    shifted = !(insideX && insideY);
                    leaderAnchorX = r.anchorX;
                    leaderAnchorY = r.anchorY;
                }
            }
        }
        if (!found) {
            // 40 halkada yer bulunamadi (asiri kalabalik): en son adayi kullan,
            // yine de deterministik kal (cakisma kacis degil, kabul).
            double bx = std::max(0.0, std::min(r.anchorX + gap, w - lw));
            double by = std::max(0.0, std::min(r.anchorY - lhgt * 0.5, h - lhgt));
            chosen = LabelBox{bx, by, lw, lhgt};
            shifted = true;
        }
        placed.push_back(chosen);
        LabelPlacement p;
        p.box = chosen;
        p.anchorX = leaderAnchorX;
        p.anchorY = leaderAnchorY;
        p.shifted = shifted;
        out[oi] = p;
    }
    return out;
}

} // namespace mm
