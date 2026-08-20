#include "model_maker/profile_database.hpp"

#include <cmath>
#include <cstdio>

int main() {
    const auto profiles = mm::parseTeklaProfileDatabase("tests/data/sample_profiles.lis");
    if (profiles.size() != 2) {
        std::printf("HATA: %zu profil beklenen 2\n", profiles.size());
        return 1;
    }
    const auto* kkr30 = mm::findProfile(profiles, "KKR30*3");
    if (!kkr30) { std::printf("HATA: KKR30*3 bulunamadi\n"); return 1; }
    if (std::abs(kkr30->height - 30.0) > 1e-6 || std::abs(kkr30->plateThickness - 3.0) > 1e-6 ||
        std::abs(kkr30->crossSectionArea - 301.0) > 1e-6 ||
        std::abs(kkr30->weightPerUnitLength - 2.36) > 1e-3 ||
        std::abs(kkr30->inertiaX - 35000.0) > 1e-3 ||
        std::abs(kkr30->sectionModulusX - 2339.999914) > 1e-3) {
        std::printf("HATA: KKR30*3 ozellikleri yanlis (h=%.1f t=%.1f A=%.1f G=%.3f Ix=%.1f Wx=%.1f)\n",
                    kkr30->height, kkr30->plateThickness, kkr30->crossSectionArea,
                    kkr30->weightPerUnitLength, kkr30->inertiaX, kkr30->sectionModulusX);
        return 1;
    }
    const auto* kkr40 = mm::findProfile(profiles, "kkr40*4"); // kucuk harf arama
    if (!kkr40 || std::abs(kkr40->height - 40.0) > 1e-6) {
        std::printf("HATA: kkr40*4 bulunamadi\n"); return 1;
    }
    std::printf("PROFILE-DB OK — %zu profil: KKR30*3 (A=%.0f mm², Ix=%.0f mm⁴, G=%.2f kg/m), "
                "KKR40*4 (A=%.0f mm²)\n",
                profiles.size(), kkr30->crossSectionArea, kkr30->inertiaX,
                kkr30->weightPerUnitLength, kkr40->crossSectionArea);
    return 0;
}
