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

// ---------------------------------------------------------------------------
// GEOMETRI SURUM KIMLIGI (geometry version id).
//
// Bir varligin (EntityId) B-Rep geometrisi zamanla DEGISEBILIR (trim, kolon
// kotu, profil atama). O geometrinin her ayri hali IMMUTABLE bir SURUMDUR ve
// kendi VersionId'siyle adreslenir. Boylece "undo anindaki geometri" geri
// yuklenebilir: kimligi geri yuklemek YETMEZ (ayni kimlik altinda yeni
// geometri uretilebilir), surum de gerekir.
//
// Sozlesme (EntityId'den ONEMLI FARKI asagida "TUKENME"):
//   * 0 GECERSIZ (kInvalidVersionId); surumler 1'den baslar.
//   * Monoton artar ve ASLA TEKRAR KULLANILMAZ — budama/etkinlestirme diger
//     surumlerin kimligini DEGISTIRMEZ.
//   * TUKENME (exhaustion): sayac kMax'i urettikten sonra kInvalidVersionId'ye
//     gecer. Boylece AYNI id ikinci kez URETILMEZ.
//     (EntityId sayaci ust sinira KELEPCELENIR ve kimlik yalnizca 2^64 sonrasi
//     tekrar kullanilabilir — erisilemez bir durum. VersionId icin ayni id'nin
//     tekrar uretilmesi magazada YANLIS geometri dondururdu: ayni slot'ta
//     eslesen ILK surum kazanir. Bu yuzden burada tekrar uretim somut olarak
//     engellenir.)
//
// SAHIPLIK: surum kimligi URETIMI somut B-Rep magazasindadir (OccSolidStore).
// Bu tip yalnizca KIMLIKTIR — hicbir OCCT tipi icermez; bu yuzden UndoOp
// (core, OCC'siz) tarafindan da tasinabilir.
using VersionId = std::uint64_t;

// Gecersiz/atanmamis surum kimligi. Ayni zamanda "surum kimligi TUKENDI" isareti.
inline constexpr VersionId kInvalidVersionId = 0;

// Surum sayacini ilerletir; URETILECEK sonraki id'yi verir.
//   * current == kInvalidVersionId -> TUKENDI, kalir (0).
//   * current == kMax              -> kMax son gecerli id; sonrasi 0 (tukendi).
//   * aksi halde                   -> current + 1.
// Boylece 1..kMax id'leri TAM BIR KEZ uretilir; 0 yalnizca "tukendi"
// isaretidir ve GECERLI id olarak donmez.
inline VersionId advanceVersionIdCounter(VersionId current) noexcept {
    constexpr VersionId kMax = std::numeric_limits<VersionId>::max();
    if (current == kInvalidVersionId) return kInvalidVersionId;  // tukendi, kalir
    if (current >= kMax) return kInvalidVersionId;               // kMax son gecerli id
    return current + 1;
}

// ---------------------------------------------------------------------------
// GEOMETRI SURUM REFERANSI (undo/redo icin).
//
// Bir undo/redo kaydinin "su varligin geometrisi su SURUMDEYDI" bilgisini
// tasir. HICBIR OCCT tipi icermez (yalnizca iki tamsayi) — bu yuzden UndoOp/
// UndoRecord (document.hpp, core, OCC-kapali derlemede de gecerli) tarafindan
// tasinabilir. `version == kInvalidVersionId` O VARKLIK ICIN ETKIN B-Rep
// BULUNMADIGINI temsil eder (or. yeni olusturulan varligin undo oncesi hali):
// bu GECERLI bir durumdur, hata degildir.
struct GeometrySlotRef {
    EntityId entity{kInvalidEntityId};
    VersionId version{kInvalidVersionId};
};

} // namespace mm
