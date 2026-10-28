#pragma once

// DOGRUSAL KOPYA / TASIMA icin SAYISAL oteleme cozumleyici (portatif cekirdek).
//
// Kullanici akisi: secim (pick) -> baz noktasi -> hedef adimi. Hedef adiminda
// nokta tiklamak yerine mesafe yazilabilir:
//   "dx,dy,dz"  -> X/Y/Z otelemesi (mm)
//   "dx,dy"     -> dx, dy (dz = 0)
//   "d"         -> baz noktasindan IMLEC yonunde d mm (yon yoksa +X)
// Ayirici: virgul, noktali virgul veya bosluk. Gecersiz girdi -> nullopt
// (cagiran uyari verir; komut devam eder).
//
// Bu birim OCC'siz/GUI'siz test edilir (tests/axis_delta_test.cpp).

#include "model_maker/geometry.hpp"

#include <optional>
#include <string>

namespace mm {

std::optional<Vec3> parseAxisDeltaText(const std::wstring& text,
                                       const std::optional<Vec3>& base = std::nullopt,
                                       const std::optional<Vec3>& cursor = std::nullopt);

} // namespace mm
