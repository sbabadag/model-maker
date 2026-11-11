#pragma once

// ============================================================================
// mm_component_contract SEMVER — Studio ile Model Maker arasindaki ortak
// sozlesmenin surumu. PUBLIC yuzey (CONTRACT.md'de listelenen imzalar) degisirse
// minor/major artar; liste DISINDAKI ic kod serbesttir.
//
//   patch = davranis degismeyen duzeltme (ABI/API ayni)
//   minor = geriye uyumlu ekleme (yeni tip/alan/fonksiyon, mevcut imza bozulmaz)
//   major = kirici degisiklik (imza/alan kaldirildi veya semantigi degisti)
//
// Paketler manifest'te minContractVersion bildirir; yukleyici contractSupports
// ile uyumluluk denetler.
// ============================================================================
namespace mm {

inline constexpr int kContractVersionMajor = 1;
inline constexpr int kContractVersionMinor = 23;
inline constexpr int kContractVersionPatch = 0;

// Tek string semver: "1.23.0".
// NOT: sayisal alanlar ile string AYNI tutulur — `contractSupports` sayisal
// alanlari, paket dogrulamasi string'i kullanir; ikisi ayrisirsa (1.22.0
// string'ine karsilik minor=11 kalmasi gibi) minContractVersion bildiren
// Studio paketleri sessizce REDDEDILIR. Test bunu kilitler (sectionP2).
inline constexpr const char* kContractVersion = "1.23.0";

// Istenen min surum mevcut kontrat tarafindan karsilaniyor mu?
// Semver: major FARKLIYSA desteklenmez (ne ust ne alt); ayni majorda
// kontrat minor'u istenen min minor'dan kucuk degilse desteklenir.
inline constexpr bool contractSupports(int minMajor, int minMinor) noexcept {
    return kContractVersionMajor == minMajor && kContractVersionMinor >= minMinor;
}

} // namespace mm
