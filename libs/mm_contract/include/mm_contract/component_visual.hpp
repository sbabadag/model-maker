#pragma once

// ============================================================================
// LEVHA (Plate) GORUNUM KURALI — KALICI, TEK KAYNAK.
//
// Kural (kullanici karari): levhalar HER ZAMAN gridir — komponent onizlemesinde,
// canli mock view'da ve modelde. Secim/hover vurgusu bu kuralin USTUNDE calisir
// (vurgu levha rengini degistirmez, ustune cizer). Profiller ve bulonlar mevcut
// renklerini korur.
//
// Deger TEK yerde tanimlidir: Studio (mmcs) bu sabite baglanir; iki tarafta
// ayri sabit tutmak renk kaymasinin klasik kaynagidir.
// ============================================================================

#include <cstdint>

namespace mm {

// 0xRRGGBB — EntityProperties::effectiveColor ile ayni kodlama.
inline constexpr std::uint32_t kPlateGreyRgb = 0xAAAAAAu;  // RGB(170, 170, 170)

inline constexpr unsigned kPlateGreyR = (kPlateGreyRgb >> 16) & 0xFFu;
inline constexpr unsigned kPlateGreyG = (kPlateGreyRgb >> 8) & 0xFFu;
inline constexpr unsigned kPlateGreyB = kPlateGreyRgb & 0xFFu;

static_assert(kPlateGreyR == 170u && kPlateGreyG == 170u && kPlateGreyB == 170u,
              "levha grisi RGB(170,170,170) olmali (Studio ile ortak sabit)");

}  // namespace mm
