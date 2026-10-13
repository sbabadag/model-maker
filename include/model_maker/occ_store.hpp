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
};

// OCC-kapali no-op saglayici (model_maker_core).
std::unique_ptr<OccStore> makeNullOccStore();

}  // namespace mm
