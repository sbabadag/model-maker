#pragma once

// MM-7 — BULON/UYE CAKISMA + EC3 MIN KENAR/ARALIK KONTROLU (OCCT'siz, Qt'siz).
//
// Bir GenerationPlan'in bulonlarini ikincil uyelerin kesit CELIGIYLE (baslik/
// govde izdusumu) cakisma ve EC3 minimum kenar/aralik kurallarina karsi denetler:
//   - delik (d0 + kaynak payi, varsayilan 10 mm) baslik/govde izdusumuyle
//     cakismamali (I-profil: iki baslik + govde dikdortgeni);
//   - kenar mesafeleri e1/e2 >= 1.2*d0;
//   - araliklar p1 >= 2.2*d0 (kuvvet yonu), p2 >= 2.4*d0 (kuvvete dik).
//
// Sonuc, GenerationPlan.checks[]'e yazilir ({id, pass, value, limit, message});
// ihlal, cagiranin plan.ok=false + Turkce mesaj vermesi icin pass=false olur.
// Studio'nun salt okunur OCCT incelemesi AYNI kurali bagimsiz dogrular (checks[]
// raporlanabilir alan oldugu icin karsilastirilabilir).

#include "model_maker/component_engine.hpp"

#include <string>
#include <vector>

namespace mm {

// Tek kontrolun sonucu `ComponentCheck` component_engine.hpp'te tanimlidir
// (GenerationPlan.checks[] icin); bu baslik yalniz kontrol URETIMINI verir.

// Kontrol esikleri. Varsayilanlar COMPONENT-PACKAGE-01 §4.3 / EC3 minimumlari.
struct ComponentCheckOptions {
    double weldAllowance{10.0};  // kaynak payi: delik kenarindan celige NET mesafe (mm)
    double minEdgeRatio{1.2};    // e1/e2 >= 1.2*d0
    double minPitch1Ratio{2.2};  // p1 >= 2.2*d0 (kuvvet yonu / dikey sira)
    double minPitch2Ratio{2.4};  // p2 >= 2.4*d0 (kuvvete dik / yatay kolon)
};

// Plani denetler; her (bulon grubu x ikincil uye) icin cakisma, her bulon grubu
// icin kenar/aralik kontrolu uretir. Kesit kalinliklari bilinmeyen uyelerde
// (tw/tf <= 0) cakisma kontrolu ATLANIR (guvenli: bilinmeden yanlis ret yok).
std::vector<ComponentCheck> runComponentChecks(const GenerationPlan& plan,
                                               const ComponentInput& input,
                                               const ComponentCheckOptions& opts = {}) noexcept;

bool allChecksPass(const std::vector<ComponentCheck>& checks) noexcept;

// Basarisiz kontrollerin toplu Turkce mesaji (tek satirda birlestirilir).
std::wstring failingChecksMessage(const std::vector<ComponentCheck>& checks) noexcept;

}  // namespace mm
