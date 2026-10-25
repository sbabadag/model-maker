#include "model_maker/profile_database.hpp"
#include "model_maker/document.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <filesystem>

int main() {
    const auto fixture = std::filesystem::path(__FILE__).parent_path() /
                         "data" / "sample_profiles.lis";
    const auto profiles = mm::parseTeklaProfileDatabase(fixture);

    // Gömülü Avrupa kataloğu: 123 profil (HEA/HEB/HEM/IPE/IPN/UPN)
    const auto european = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "profiles" / "european.lis";
    const auto euProfiles = mm::parseTeklaProfileDatabase(european);
    if (euProfiles.size() != 123) {
        std::printf("HATA: Avrupa katalogu %zu profil (123 bekleniyor)\n",
                    euProfiles.size());
        return 1;
    }
    const auto hea300 = std::find_if(euProfiles.begin(), euProfiles.end(),
                                     [](const mm::SteelProfile& profile) {
                                         return profile.name == "HEA300";
                                     });
    if (hea300 == euProfiles.end() ||
        std::abs(hea300->crossSectionArea - 11250.0) > 50.0) {
        std::printf("HATA: HEA300 yok veya alan yanlis (A=%.1f mm2)\n",
                    hea300 == euProfiles.end() ? -1.0 : hea300->crossSectionArea);
        return 1;
    }
    std::printf("EU-PROFILES OK — 123 profil, HEA300 A=%.1f mm2, Ix=%.0f mm4\n",
                hea300->crossSectionArea, hea300->inertiaX);
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

    mm::Document document;
    document.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    document.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
    document.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});
    if (document.setModelProfile({0, 2}, kkr30->name) != 2 ||
        document.models()[0].properties().profileName != "KKR30*3" ||
        !document.models()[1].properties().profileName.empty() ||
        document.models()[2].properties().profileName != "KKR30*3") {
        std::printf("HATA: profil yalniz secili cizgilere atanamadi\n");
        return 1;
    }
    std::printf("PROFILE-DB OK — %zu profil: KKR30*3 (A=%.0f mm², Ix=%.0f mm⁴, G=%.2f kg/m), "
                "KKR40*4 (A=%.0f mm²)\n",
                profiles.size(), kkr30->crossSectionArea, kkr30->inertiaX,
                kkr30->weightPerUnitLength, kkr40->crossSectionArea);
    // Kesit turu + UST YUZEY REFERANSI (KULLANICI SARTI v4: "ust flansin
    // USTUNDEN cizmiyor, ust flansin MERKEZINDEN ciziyor"). Formul portatiftir
    // (OCC'siz de kosar); gercek geometri tests/occ_smoke.cpp icinde gercek
    // OCCT ile olculur.
    {
        mm::SteelProfile ipe;
        ipe.name = "IPE200";
        ipe.height = 200.0;
        ipe.width = 100.0;
        ipe.plateThickness = 5.6;
        ipe.flangeThickness = 8.5;
        const bool kindOk =
            mm::classifyProfileSection(ipe) == mm::ProfileSectionKind::ISection &&
            mm::classifyProfileSection(*kkr30) == mm::ProfileSectionKind::Box;
        // I-kesit: ust flansin UST yuzu = h/2 = 100 (v3'te 95.75 idi)
        const bool ipeOk = std::abs(mm::profileSectionTopY(ipe) - 100.0) < 1e-9;
        // ici bos kutu (KKR30*3: h=30, t=3): ust plakanin UST yuzu = 15
        // (dolu/bos ayrimi YOKTUR; v3'te 13.5 idi)
        const bool kkrOk = std::abs(mm::profileSectionTopY(*kkr30) - 15.0) < 1e-9;
        mm::SteelProfile chs;
        chs.name = "CFCHS127x2.5";
        chs.height = 127.0;
        chs.width = 127.0;
        chs.plateThickness = 2.5;
        // boru: ust nokta DIS yaricap = 63.5 (duvar ortasi 62.25 DEGIL)
        const bool chsOk = mm::classifyProfileSection(chs) == mm::ProfileSectionKind::Round &&
                           std::abs(mm::profileSectionTopY(chs) - 63.5) < 1e-9;
        // Dolu yuvarlak: ust yuz = yaricap = 10
        mm::SteelProfile rod;
        rod.name = "ROD20";
        rod.height = 20.0;
        rod.width = 20.0;
        const bool rodOk = mm::classifyProfileSection(rod) == mm::ProfileSectionKind::Round &&
                           std::abs(mm::profileSectionTopY(rod) - 10.0) < 1e-9;
        if (!(kindOk && ipeOk && kkrOk && chsOk && rodOk)) {
            std::printf("HATA: kesit turu/ust yuz referansi yanlis "
                        "(IPE200=%.3f KKR30*3=%.3f CFCHS127x2.5=%.3f ROD20=%.3f)\n",
                        mm::profileSectionTopY(ipe),
                        mm::profileSectionTopY(*kkr30),
                        mm::profileSectionTopY(chs),
                        mm::profileSectionTopY(rod));
            return 1;
        }
        std::printf("SECTION-PLACEMENT OK — ust YUZ uye ekseninde (ortalanmis kesitten): "
                    "IPE200 100.0, KKR30*3 15.0, CFCHS127x2.5 63.5, ROD20 10.0\n");
    }
    return 0;
}
