// Track/guide renginin tuval fonuna gore zit (okunur) oldugunu dogrular.
#include "model_maker/color_contrast.hpp"

#include <cstdio>

using namespace mm;
using namespace mm::color_contrast;

static int failures = 0;
static void check(bool ok, const char* what, double value) {
    std::printf("%s %-58s %.2f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

int main() {
    const std::uint32_t top = kCanvasGradientTopRgb, bottom = kCanvasGradientBottomRgb;
    const std::uint32_t guide = trackGuideRgb();
    std::printf("track guide rengi: (%u,%u,%u)\n", (guide >> 16) & 255u, (guide >> 8) & 255u,
                guide & 255u);

    check(contrastRatio(guide, top) >= 4.5, "guide vs gradient ust (>=4.5)", contrastRatio(guide, top));
    check(contrastRatio(guide, bottom) >= 4.5, "guide vs gradient alt (>=4.5)",
          contrastRatio(guide, bottom));

    // Eski renkler gercekten zayifti (regresyon kaniti).
    check(contrastRatio(0x50E1FFu, top) < 2.0, "eski cyan (80,225,255) zayifti", contrastRatio(0x50E1FFu, top));
    check(contrastRatio(0xFFCE54u, top) < 2.0, "eski sari (255,206,84) zayifti", contrastRatio(0xFFCE54u, top));

    // Ton: mavi fonun tamamlayicisi -> turuncu/kahve (kirmizi baskin, mavi en az).
    const unsigned r = (guide >> 16) & 255u, g = (guide >> 8) & 255u, b = guide & 255u;
    check(r > g && g > b, "ton mavi fonun tamamlayicisi (turuncu)", static_cast<double>(r));

    // Koyu fona uyum: siyah/koyu gri fonda ACIK renk uretmeli.
    const std::uint32_t dark = contrastingGuideRgb(0x1E1E1Eu, 0x2A2A30u);
    check(relativeLuminance(dark) > 0.3 && contrastRatio(dark, 0x2A2A30u) >= 4.5,
          "koyu fonda acik ve zit renk", contrastRatio(dark, 0x2A2A30u));

    for (const auto [rgb, name] : {std::pair{kGuideAxisXRgb, "X eksen guide vs fon (>=3)"},
                                   std::pair{kGuideAxisYRgb, "Y eksen guide vs fon (>=3)"},
                                   std::pair{kGuideAxisZRgb, "Z eksen guide vs fon (>=3)"}}) {
        const double ratio = std::min(contrastRatio(rgb, top), contrastRatio(rgb, bottom));
        check(ratio >= 3.0, name, ratio);
    }
    std::printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
