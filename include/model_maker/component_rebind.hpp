#pragma once

// COMPONENT REBIND (C4) — kopyalanan component'in YENI KONUMUNDA ana/ara
// parcalari bulma. Tekla davranisi (kullanici istegi 2026-10-28): "component tek
// basina bile kopyalansa, kopyalandigi koordinatta ana ve ara objeler varsa
// kendisi otomatik baglantiyi kurmali".
//
// Kural: component'in kendi nesneleri (levha + bulon) kopyalandiginda tasinan
// CERCEVE bir capa noktasi verir; bu capa cevresindeki yapisal uyeler (profilli
// modeller) aranir. Rol secimi ORIJINAL baglantinin yonune gore yapilir:
//   main      : ekseni orijinal main eksenine PARALEL, capaya en yakin uye
//   secondary : main'e DIK, capaya en yakin uye (orijinal secondary yonleri tercih)
// Boylece ayni birlesim baska bir kiris/kolon ciftine tasindiginda da dogru
// parcalara baglanir. Profil/kimlik esitligi ARANMAZ — kesit farkli olabilir.
//
// Bu katman OCC'siz ve GUI'siz test edilebilir: girdi saf geometri (eksen
// uclari + kesit olculeri), cikti EntityId.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <vector>

namespace mm {

// Aday yapisal uye (caginan Document'tan doldurur).
struct RebindCandidate {
    EntityId id{kInvalidEntityId};
    Vec3 axisFrom{};
    Vec3 axisTo{};
    double secWidth{0.0};
    double secHeight{0.0};
};

struct RebindResult {
    bool ok{false};
    EntityId main{kInvalidEntityId};
    std::vector<EntityId> secondary;
    // Teshis: secilen adaylarin capaya dik uzakligi (mm).
    double mainDistance{0.0};
    std::vector<double> secondaryDistances;
};

// `anchor` cevresinde (toleranceMm yaricapi) main + `requiredSecondary` adet ara
// parca arar. `mainDirHint` sifir vektor ise yon kisiti uygulanmaz (yalniz
// uzaklik). `secondaryDirHints` verilirse her ara parca icin sirasiyla en iyi
// uyum aranir; bos ise main'e dik en yakin adaylar secilir.
// Ayni aday iki role birden atanmaz. main bulunamazsa ok=false.
RebindResult findRebindParts(const std::vector<RebindCandidate>& candidates,
                             const Vec3& anchor,
                             const Vec3& mainDirHint,
                             const std::vector<Vec3>& secondaryDirHints,
                             std::size_t requiredSecondary,
                             double toleranceMm);

// Nokta -> eksen PARCASI (segment) dik uzakligi; segment disina dusen izdusum
// en yakin uca kelepcelenir. Sifir uzunlukta eksen icin nokta uzakligi.
double distancePointToAxis(const Vec3& p, const Vec3& a, const Vec3& b) noexcept;

} // namespace mm
