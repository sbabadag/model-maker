#pragma once

// Arka fona gore ZIT renk hesabi (track / guide cizgileri icin).
// Tasinabilir (windows.h yok) — Linux testinde dogrudan dogrulanir.
// Renkler 0xRRGGBB (duz RGB) — COLORREF (0x00BBGGRR) DEGIL.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mm {

// Tuval gradient'i: ust acik mavi -> alt beyaza yakin. Tek kaynak.
constexpr std::uint32_t kCanvasGradientTopRgb = 0xC6E0F6u;    // (198,224,246)
constexpr std::uint32_t kCanvasGradientBottomRgb = 0xF0F8FCu; // (240,248,252)

namespace color_contrast {

inline double channel(std::uint32_t rgb, int shift) noexcept {
    return static_cast<double>((rgb >> shift) & 0xFFu) / 255.0;
}

// WCAG 2.x goreli parlaklik.
inline double relativeLuminance(std::uint32_t rgb) noexcept {
    const auto lin = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(channel(rgb, 16)) + 0.7152 * lin(channel(rgb, 8)) +
           0.0722 * lin(channel(rgb, 0));
}

// WCAG kontrast orani (1..21).
inline double contrastRatio(std::uint32_t a, std::uint32_t b) noexcept {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

inline std::uint32_t mixRgb(std::uint32_t a, std::uint32_t b) noexcept {
    std::uint32_t out = 0;
    for (int shift : {16, 8, 0}) {
        const std::uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca + cb + 1u) / 2u) << shift;
    }
    return out;
}

inline std::uint32_t hslToRgb(double h, double s, double l) noexcept {
    const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double hp = std::fmod(h, 360.0) / 60.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    const double m = l - c / 2.0;
    const auto to8 = [m](double v) {
        return static_cast<std::uint32_t>(std::clamp(std::lround((v + m) * 255.0), 0L, 255L));
    };
    return (to8(r) << 16) | (to8(g) << 8) | to8(b);
}

// Arka fonun TAMAMLAYICI tonu (hue + 180), parlaklik ters tarafa itilir:
// acik fonda koyu, koyu fonda acik. Gri fonda ton tanimsiz -> magenta.
// Gradient'in IKI ucuna karsi da en az minRatio kontrast saglanana kadar
// koyulastirilir/acilir.
inline std::uint32_t contrastingGuideRgb(std::uint32_t bgA, std::uint32_t bgB,
                                         double minRatio = 4.5) noexcept {
    const std::uint32_t avg = mixRgb(bgA, bgB);
    const double r = channel(avg, 16), g = channel(avg, 8), b = channel(avg, 0);
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    double hue = 300.0;
    if (d > 1e-6) {
        if (mx == r) hue = 60.0 * std::fmod((g - b) / d + 6.0, 6.0);
        else if (mx == g) hue = 60.0 * ((b - r) / d + 2.0);
        else hue = 60.0 * ((r - g) / d + 4.0);
        hue = std::fmod(hue + 180.0, 360.0);
    }
    const bool lightBg = relativeLuminance(avg) > 0.18;
    double l = lightBg ? 0.38 : 0.68;
    std::uint32_t c = hslToRgb(hue, 1.0, l);
    for (int i = 0; i < 40; ++i) {
        if (std::min(contrastRatio(c, bgA), contrastRatio(c, bgB)) >= minRatio) break;
        l += lightBg ? -0.02 : 0.02;
        l = std::clamp(l, 0.0, 1.0);
        c = hslToRgb(hue, 1.0, l);
    }
    return c;
}

} // namespace color_contrast

// Tuval fonuna gore track/guide rengi (0xRRGGBB).
inline std::uint32_t trackGuideRgb() noexcept {
    static const std::uint32_t color = color_contrast::contrastingGuideRgb(
        kCanvasGradientTopRgb, kCanvasGradientBottomRgb);
    return color;
}

// Ortho eksen guide renkleri — acik fonda okunur koyu tonlar (X/Y/Z).
constexpr std::uint32_t kGuideAxisXRgb = 0xBE192Du; // (190,25,45)
constexpr std::uint32_t kGuideAxisYRgb = 0x00782Du; // (0,120,45)
constexpr std::uint32_t kGuideAxisZRgb = 0x1446BEu; // (20,70,190)

} // namespace mm
