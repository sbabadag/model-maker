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

// ============================================================================
// PARAMETRE VURGU RENGI (odak/hover) — KALICI, TEK KAYNAK.
//
// Kural (kullanici karari, MM-8b sonrasi duzeltme): odak/hover olcu vurgusu
// KIRMIZI OLAMAZ — kirmizi, kontrol ihlali (failed check) icin ayrilmistir.
// Vurgu turuncudur. Bu sabit hem 2B sematikte hem 3B onizlemede kullanilir;
// iki tarafta ayri sabit tutmak renk kaymasinin klasik kaynagidir.
// ============================================================================

inline constexpr std::uint32_t kParamHighlightRgb = 0xD47B2Cu;  // RGB(212,123,44)

inline constexpr unsigned kParamHighlightR = (kParamHighlightRgb >> 16) & 0xFFu;
inline constexpr unsigned kParamHighlightG = (kParamHighlightRgb >> 8) & 0xFFu;
inline constexpr unsigned kParamHighlightB = kParamHighlightRgb & 0xFFu;

static_assert(kParamHighlightR == 212u && kParamHighlightG == 123u &&
                  kParamHighlightB == 44u,
              "vurgu turuncu RGB(212,123,44) olmali (ihlal kirmizisindan ayri)");

}  // namespace mm
