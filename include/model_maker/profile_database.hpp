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
    double flangeThickness{};
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

// Profil adlarini dogal (sayisal farkindalikli) sirada karsilastirir:
// HEA100 < HEA120 < HEA1000 (duz std::string sirasinda HEA1000 < HEA120
// olurdu) ve IPE80 < IPE100. Harf karsilastirmasi buyuk/kucuk harf duyarsiz.
bool profileNameLess(const std::string& a, const std::string& b);

// Kesit turu. Kati ureticisi (extrudeProfileSolid) ile kesit yerlesim
// kurallari AYNI siniflandirmayi kullansin diye tek kaynak (eskiden bu
// harf kumesi kontrolu occ_geometry.cpp icinde yereldi).
enum class ProfileSectionKind { ISection, Round, Box };

// Profil ADININ rakamdan onceki harf kumesinden tur:
// CHS/CFCHS/ROD/D/P/TUBE/O → Round; HE*/IPE*/IPN*/UB*/UC*/HL*/HD*/HP*/W*/T →
// ISection; digerleri → Box (KKR/RHS/SHS ve bilinmeyenler).
ProfileSectionKind classifyProfileSection(const SteelProfile& profile) noexcept;

// KULLANICI SARTI (v4): "ust flansin USTUNDEN cizmiyor, ust flansin
// MERKEZINDEN ciziyor" → kesit uye eksenine UST YUZEYINDEN oturur: eksen
// cizgisi kesitin en ustunden gecer ve TUM kesit cizginin ALTINA sarkar
// (Tekla "position: top of section"). v3'te ust flansin/plakanin ORTASI
// oturuyordu; baslik paralelligi (v1/v2) DEGISMEZ, degisen yalnizca DUSEY
// KONUMdur.
//
// Donen deger: kesit ORTALANMIS uretildiginde UST YUZEYIN yerel Y'si; kati
// bu kadar ASAGI (-Y) kaydirilir.
//   I-kesit  : h/2   (ust flansin UST yuzu)
//   kutu/boru: h/2   (ust plakanin UST yuzu — dolu/bos ayrimi yok)
//   yuvarlak : max(w,h)/2  (dis yaricap; dolu/bos ayrimi yok)
double profileSectionTopY(const SteelProfile& profile) noexcept;

// KESIT YERLESIM ANCHOR'I (Slice 2d-2c P3). Uye ekseni kesitin neresinden
// gecer? Mevcut davranis Top ("top of section"): eksen kesitin EN USTUNDEN
// gecer, kesit altina sarkar (Tekla "position: top of section"). Bu, mevcut
// modellerin KALICI dogruluk kaynagi olan davranistir; DEFAULT Top kalir ve
// var olan belgeler SESSIZCE baska bir anchor'a DONUSTURULMEZ. Center/Bottom
// ileride kullanici secenekleri olarak eklenebilir.
enum class SectionAnchor {
    Top,     // kesit ust yuzeyi eksende (mevcut davranis)
    Center,  // kesit merkezi/orijini eksende
    Bottom,  // kesit alt yuzeyi eksende
};

// Bir anchor icin kesitin UYE EKSENINE gore DUSEY ofsetini dondurur (katiya
// uygulanacak Y otelemesi). Top -> -profileSectionTopY (ust yuzey eksene),
// Center -> 0, Bottom -> +profileSectionTopY (alt yuzey eksene; kesit orijine
// gore simetrik uretildiginden alt yuzey = -topY). Round/Box/ISection icin
// ayni kural gecerlidir.
double profileSectionAnchorOffset(const SteelProfile& profile, SectionAnchor anchor) noexcept;

} // namespace mm
