#pragma once

// OPAK B-Rep magaza arayuzu (OCCT-SIZ).
//
// Document, B-Rep sekillerini bu arayuz uzerinden SAHIPLENIR. Bu baslik HICBIR
// OCCT basligi icermez; sozlesmesi (imzalar) yalnizca EntityId ve standart
// tipler kullanir. Boylece:
//   * document.hpp OCCT'ye bagimli OLMAZ (OCC-kapali derleme korunur),
//   * TopoDS_Shape genis include edilen basliklara SIZMAZ,
//   * somut OCCT uygulamasi (bkz. occ_solid_store.hpp) yalnizca OCC-acik
//     derleme birimlerinde ORTAYA CIKAR ve Document'a ENJEKTE edilir.
//
// Saglayici secimi LINK SIRASINA bagli DEGILDIR (weak symbol YOK):
//   * makeNullOccStore()  -> model_maker_core (her zaman link; no-op tablo).
//     Document varsayilan olarak bununla kurulur.
//   * makeOccSolidStore() -> model_maker_occ (MM_HAS_OCC). Application,
//     OCC-acik derlemede bunu uretip Document'a enjekte eder.

#include "model_maker/entity_id.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace mm {

class OccStore {
public:
    virtual ~OccStore() = default;

    // Tum B-Rep sekillerini birakir (belge temizleme/yukleme).
    virtual void clear() noexcept = 0;

    // Tutulan sekil (kayit) sayisi.
    virtual std::size_t size() const noexcept = 0;

    bool empty() const noexcept { return size() == 0; }

    // --- SURUM SORGULARI (undo/redo icin; TAM SAYI tabanli, OCCT'siz) -------
    // Bu uc metot, Document'in undo/redo kayitlarindaki GeometrySlotRef
    // referanslarini DOGRULAMAK ve UYGULAMAK icin vardir. Imzalarinda OCCT
    // tipi YOKTUR; bu yuzden abstract arayuz OCCT'siz kalir ve OCC-kapali
    // derlemede de tanimlidir.

    // O varligin ETKIN surum kimligi; yoksa/geometri yoksa kInvalidVersionId.
    virtual VersionId activeVersion(EntityId id) const noexcept = 0;

    // Verilen surum o varlik icin MEVCUT MU (undo ONCESI dogrulama).
    // kInvalidVersionId "geometri yok" demektir ve GECERLI sayilir (true).
    virtual bool findVersionExists(EntityId id, VersionId version) const = 0;

    // Verilen surumu ETKIN yapar (undo/redo UYGULAMASI).
    // kInvalidVersionId = "bu varlik icin etkin geometri YOK" (GECERLI).
    // Surum gercekten yoksa false doner ve hicbir sey degismez.
    // (Mevcut `activate()` sozlesmesi BOZULMAZ; bu ayri bir yoldur cunku
    //  activate kInvalidVersionId'yi reddeder — oysa restoreActive onu kabul
    //  eder: olusturma-undo'da geometri "yok" haline donmelidir.)
    virtual bool restoreActive(EntityId id, VersionId version) = 0;

    // --- KESME TABAN SURUMU (Phase 4.2) -------------------------------------
    // Bir varligin KESILMEMIS (pre-cut) B-Rep surumu. Kesme modifier'lari bu
    // surumden baslanarak sirasiyla yeniden uygulanir (recompute). Boylece
    // "orijinal B-Rep kaybolmaz": kesme islemleri etkin surumu ilerletir ama
    // taban surumu SABIT kalir; silme/tasima/duzenleme tabandan yeniden turetilir.
    // ACIK ve GUVENILIR referanstir (versionIds()[0] varsayimi YOKTUR) ve
    // budamadan (prune) KORUNUR — Document keep kumesine taban surumleri ekler.
    // Atanmamissa / geometri yoksa kInvalidVersionId doner.

    // O varligin taban (pre-cut) surum kimligi; yoksa/geometri yoksa kInvalid.
    virtual VersionId baseVersion(EntityId id) const noexcept = 0;

    // Taban surumunu ISARETLER. `version == kInvalidVersionId` tabani SILER
    // (her zaman true). Gercek surum icin: varlik yoksa VEYA surum yoksa false
    // doner ve hicbir sey degismez (taban var olan bir surume isaret etmelidir).
    virtual bool setBaseVersion(EntityId id, VersionId version) = 0;

    // --- SURUM BUDAMA (Phase 3.2C) -----------------------------------------
    // `keep(entity, version)` false donen her saklanan surumu SILER; bosalan
    // slot'lari kaldirir ve budanan surumu isaret eden etkin-alani "geometri
    // yok"a dusurur. Budanan surum sayisini doner. Imza OCCT'sizdir (yalnizca
    // EntityId/VersionId + std::function), boylece OCC-kapali derleme de
    // bozulmaz. KORUMA SORUMLULUGU cagirandadir: Document canli referans
    // kumesini (etkin surumler + undo/redo kayitlarindaki geometryBefore/
    // geometryAfter + kimlikle referanslanan varliklar) hesaplayip buraya
    // `keep` olarak iletir.
    virtual std::size_t prune(const std::function<bool(EntityId, VersionId)>& keep) = 0;

    // --- MMW8 B-Rep SERILESTIRME (OCCT'siz imzalar; Phase 3.4C) --------------
    // Document, B-Rep'i kalicilastirirken soyut store'dan su metotlari kullanir.
    // Imzalarda OCCT tipi YOKTUR (yalniz EntityId + std::string) — boylece
    // OCC-kapali derleme bozulmaz. Somut OCCT uygulamasi (OccSolidStore) bu
    // islemleri occ_brep_serialize uzerinden gerceklestirir; no-op store bos
    // doner.

    // Etkin B-Rep'i binary blob olarak doner; geometri yoksa bos string.
    virtual std::string serializeActive(EntityId id) const = 0;

    // Binary blob'dan o varlik icin aktif B-Rep kurar (deserialize + set).
    // Basarisizlikta false doner ve `error` (null degilse) nedeni alir; magaza
    // DEGISMEZ. Bu, yuklemenin "geometri geri yuklenemedi" durumunu acikca
    // bildirmesi icindir (sessiz wireframe-only gecis YOK).
    virtual bool restoreActiveSolid(EntityId id, const std::string& blob,
                                    std::string* error) = 0;

    // --- KESME TABAN B-Rep SERILESTIRME (Phase 4.2) -------------------------
    // Taban (pre-cut) B-Rep'i binary blob doner; taban surumu yoksa bos string.
    // (Etkin B-Rep icin serializeActive; taban icin bunu kullan — boylece
    // "orijinal B-Rep kaybolmaz" kayit/ac sonrasinda da korunur.)
    virtual std::string serializeBase(EntityId id) const = 0;

    // Binary blob'dan taban B-Rep kurar: deserialize -> set (YENI surum uretir)
    // -> setBaseVersion(yeni surum). Basarisizlikta false + `error`; magaza
    // DEGISMEZ (atomik).
    virtual bool restoreBaseSolid(EntityId id, const std::string& blob,
                                  std::string* error) = 0;

    // Bu store'un kullandigi OCCT surumu (or. "7.9.2"); OCC yoksa bos string.
    virtual std::string occVersionString() const noexcept = 0;

    // B-Rep blob kodlamasinin BinTools format surumu; OCC yoksa 0.
    virtual int bRepFormatVersion() const noexcept = 0;

    // AYNI TURDE BOS bir store uretir (atomik MMW8 yukleme icin: gecici belgeye
    // taze store enjekte edilir, basari olursa tasinir; basarisizlikta asil
    // store DEGISMEMIS kalir). OCC-acik -> yeni OccSolidStore; OCC-kapali -> no-op.
    virtual std::unique_ptr<OccStore> cloneEmpty() const = 0;
};

// OCC-kapali no-op saglayici (model_maker_core).
std::unique_ptr<OccStore> makeNullOccStore();

}  // namespace mm
