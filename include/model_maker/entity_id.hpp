#pragma once

#include <cstdint>
#include <limits>

namespace mm {

// KARARLI VARLIK KIMLIGI (stable entity identity).
//
// Vektor indeksi KALICI kimlik DEGILDIR: ekleme/silme/siralama ile kayar.
// EntityId tekildir, monoton artar ve bir varligin yasam dongusu boyunca
// AYNI kalir (olustur -> degistir -> geri al/yinele). 0 GECERSIZ (sentinel)
// degerdir; kimlikler 1'den baslar.
//
// SAHIPLIK: kimlik DOKUMANin (Document) sorumlulugundadir (modelIds_ +
// sayac). Geometri degerinin (WireframeModel) kopyalanmasi kimligi
// kopyalamaz/degistirmez — kimlik, geometriden BAGIMSIZ olarak Document
// tarafinda tutulur. Bu ayrim, ileride occShapes_ / beamLoads_ gibi
// indeks-anahtarli tablolarin kimlik-anahtarli hale getirilmesini saglar.
using EntityId = std::uint64_t;

// Gecersiz/atanmamis kimlik. Hicbir gercek varlik bu degeri almaz.
inline constexpr EntityId kInvalidEntityId = 0;

// Sayaci bir adim ilerletir. Tasma (overflow) durumunda 0'a SARMAS yerine
// ust sinira KELEPCELENIR — boylece gecersiz sentinel (0) asla uretilmez.
// uint64 siniri pratikte erisilemez (1e9 id/sn'de ~584 yil); kelepce yalnizca
// sarmayi ve gecersiz id uretimini onleyen bir guvenlik agidir.
inline EntityId advanceEntityIdCounter(EntityId current) noexcept {
    constexpr EntityId kMax = std::numeric_limits<EntityId>::max();
    if (current >= kMax) return kMax;
    return current + 1;
}

} // namespace mm
