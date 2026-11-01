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
inline constexpr int kContractVersionMinor = 2;
inline constexpr int kContractVersionPatch = 0;

// Tek string semver: "1.2.0".
inline constexpr const char* kContractVersion = "1.2.0";

// Istenen min surum mevcut kontrat tarafindan karsilaniyor mu?
// (minMajor.minMinor >= kContractVersionMajor.Minor ise true).
inline constexpr bool contractSupports(int minMajor, int minMinor) noexcept {
    return kContractVersionMajor > minMajor ||
           (kContractVersionMajor == minMajor && kContractVersionMinor >= minMinor);
}

} // namespace mm
