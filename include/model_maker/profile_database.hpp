#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mm {

// Tekla profil veritabani kaydi (.lis formatindan ayrismis tek profil).
// Olculer: mm; alan: mm²; atalet: mm⁴; mukavemet modulu: mm³; agirlik: kg/m.
struct SteelProfile {
    std::string name;
    std::int32_t type{};
    std::int32_t subType{};
    double height{};
    double width{};
    double plateThickness{};
    double roundingRadius{};
    double crossSectionArea{};
    double weightPerUnitLength{};
    double inertiaX{};
    double inertiaY{};
    double sectionModulusX{};
    double sectionModulusY{};
    double plasticModulusX{};
    double plasticModulusY{};
    double radiusOfGyrationX{};
    double radiusOfGyrationY{};
    double torsionalConstant{};
    double warpingConstant{};
    double coverArea{};
};

// Tekla'nin .lis profil veritabani dosyasini (PROFILE DATABASE EXPORT)
// ayrismtirir. Cift kayitlar (ayni isim) ilk gorulen korunarak elenir.
std::vector<SteelProfile> parseTeklaProfileDatabase(const std::filesystem::path& lisFile);

// Bir dizindeki tum *.lis dosyalarini birlestirir (katalog birligi).
std::vector<SteelProfile> loadProfileCatalog(const std::filesystem::path& directory);

// Ada gore profil arama (buyuk/kucuk harf duyarsiz); bulunamazsa nullopt.
const SteelProfile* findProfile(const std::vector<SteelProfile>& profiles,
                                const std::string& name);

} // namespace mm
