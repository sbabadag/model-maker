#pragma once

#include "model_maker/geometry.hpp"

#include <vector>

namespace mm {

// DOGRUSAL KOPYA/TASIMA panelinin girdisi — Application ile Qt paneli arasindaki
// TEK yapi (alan eklemek imzayi bozmaz).
struct LinearDeltaInput {
    Vec3 delta{};                  // X/Y/Z offset (mm)
    int count{1};                  // kopya adedi (>= 1); tasimada kullanilmaz
    bool pickTwoPoints{false};     // "Iki Nokta Sec": yalniz OLCU alinacak
    bool fromPickedPoints{false};  // panel, olculen delta ile yeniden aciliyor
    bool allowCount{true};         // CopyLinear true, MoveLinear false
};

// Panel araligi ile ayni ust sinir (spinbox ve cekirdek ayrisirsa test yakalar).
inline constexpr int kLinearCopyMaxCount = 1000;

// Kopya ofsetleri: k * delta (k = 1..count). count < 1 -> bos; ust sinir kirpilir.
// Adet 1 = tek kopya (eski davranis); adet N = delta, 2*delta, ... N*delta.
std::vector<Vec3> linearCopyOffsets(const Vec3& delta, int count);

} // namespace mm
